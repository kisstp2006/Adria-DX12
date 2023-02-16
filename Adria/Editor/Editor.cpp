#include "Editor.h"
#include "GUICommand.h"
#include "nfd.h"
#include "../Rendering/Renderer.h"
#include "../Graphics/GraphicsDeviceDX12.h"
#include "../Rendering/ModelImporter.h"
#include "../Rendering/PipelineState.h"
#include "../Rendering/ShaderCache.h"
#include "../Logging/Logger.h"
#include "../Utilities/FilesUtil.h"
#include "../Utilities/StringUtil.h"
#include "../Utilities/Random.h"
#include "../Math/BoundingVolumeHelpers.h"
#include "pix3.h"

using namespace DirectX;

namespace adria
{
	//heavily based on AMD Cauldron code
	struct ProfilerState
	{
		bool  show_average = false;
		struct AccumulatedTimeStamp
		{
			float sum;
			float minimum;
			float maximum;

			AccumulatedTimeStamp()
				: sum(0.0f), minimum(FLT_MAX), maximum(0)
			{}
		};

		std::vector<AccumulatedTimeStamp> displayed_timestamps;
		std::vector<AccumulatedTimeStamp> accumulating_timestamps;
		double last_reset_time = 0.0;
		uint32 accumulating_frame_count = 0;
	};

	struct ImGuiLogger
	{
		ImGuiTextBuffer     Buf;
		ImGuiTextFilter     Filter;
		ImVector<int>       LineOffsets;
		bool                AutoScroll;

		ImGuiLogger()
		{
			AutoScroll = true;
			Clear();
		}

		void Clear()
		{
			Buf.clear();
			LineOffsets.clear();
			LineOffsets.push_back(0);
		}

		void AddLog(const char* fmt, ...) IM_FMTARGS(2)
		{
			int old_size = Buf.size();
			va_list args;
			va_start(args, fmt);
			Buf.appendfv(fmt, args);
			va_end(args);
			for (int new_size = Buf.size(); old_size < new_size; old_size++)
				if (Buf[old_size] == '\n')
					LineOffsets.push_back(old_size + 1);
		}

		void Draw(const char* title, bool* p_open = NULL)
		{
			if (!ImGui::Begin(title, p_open))
			{
				ImGui::End();
				return;
			}

			// Options menu
			if (ImGui::BeginPopup("Options"))
			{
				ImGui::Checkbox("Auto-scroll", &AutoScroll);
				ImGui::EndPopup();
			}

			// Main window
			if (ImGui::Button("Options"))
				ImGui::OpenPopup("Options");
			ImGui::SameLine();
			bool clear = ImGui::Button("Clear");
			ImGui::SameLine();
			bool copy = ImGui::Button("Copy");
			ImGui::SameLine();
			Filter.Draw("Filter", -100.0f);

			ImGui::Separator();
			ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

			if (clear)
				Clear();
			if (copy)
				ImGui::LogToClipboard();

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			const char* buf = Buf.begin();
			const char* buf_end = Buf.end();
			if (Filter.IsActive())
			{
				for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
				{
					const char* line_start = buf + LineOffsets[line_no];
					const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
					if (Filter.PassFilter(line_start, line_end))
						ImGui::TextUnformatted(line_start, line_end);
				}
			}
			else
			{
				ImGuiListClipper clipper;
				clipper.Begin(LineOffsets.Size);
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						const char* line_start = buf + LineOffsets[line_no];
						const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
						ImGui::TextUnformatted(line_start, line_end);
					}
				}
				clipper.End();
			}
			ImGui::PopStyleVar();

			if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);

			ImGui::EndChild();
			ImGui::End();
		}
	};
	class EditorLogger : public ILogger
	{
	public:
		EditorLogger(ImGuiLogger* logger, ELogLevel logger_level = ELogLevel::LOG_DEBUG) : logger{ logger }, logger_level{ logger_level } {}

		virtual void Log(ELogLevel level, char const* entry, char const* file, uint32_t line) override
		{
			if (level < logger_level) return;
			std::string log_entry = GetLogTime() + LevelToString(level) + std::string(entry) + "\n";
			if (logger) logger->AddLog(log_entry.c_str());
		}
	private:
		ImGuiLogger* logger;
		ELogLevel logger_level;
	};

	struct ImGuiConsole
	{
		char                  InputBuf[256];
		ImVector<char*>       Items;
		ImVector<const char*> Commands;
		ImVector<char*>       History;
		int                   HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
		ImGuiTextFilter       Filter;
		bool                  AutoScroll;
		bool                  ScrollToBottom;

		ImGuiConsole()
		{
			ClearLog();
			memset(InputBuf, 0, sizeof(InputBuf));
			HistoryPos = -1;

			// "CLASSIFY" is here to provide the test case where "C"+[tab] completes to "CL" and display multiple matches.
			Commands.push_back("HELP");
			Commands.push_back("HISTORY");
			Commands.push_back("CLEAR");
			Commands.push_back("CLASSIFY");
			AutoScroll = true;
			ScrollToBottom = false;
			AddLog("Welcome to Dear ImGui!");
		}
		~ImGuiConsole()
		{
			ClearLog();
			for (int i = 0; i < History.Size; i++)
				free(History[i]);
		}

		// Portable helpers
		static int   Stricmp(const char* s1, const char* s2) { int d; while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; } return d; }
		static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }
		static char* Strdup(const char* s) { IM_ASSERT(s); size_t len = strlen(s) + 1; void* buf = malloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)s, len); }
		static void  Strtrim(char* s) { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }

		void    ClearLog()
		{
			for (int i = 0; i < Items.Size; i++)
				free(Items[i]);
			Items.clear();
		}

		void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
		{
			// FIXME-OPT
			char buf[1024];
			va_list args;
			va_start(args, fmt);
			vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
			buf[IM_ARRAYSIZE(buf) - 1] = 0;
			va_end(args);
			Items.push_back(Strdup(buf));
		}

		void    Draw(const char* title, bool* p_open)
		{
			ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
			if (!ImGui::Begin(title, p_open))
			{
				ImGui::End();
				return;
			}

			// As a specific feature guaranteed by the library, after calling Begin() the last Item represent the title bar.
			// So e.g. IsItemHovered() will return true when hovering the title bar.
			// Here we create a context menu only available from the title bar.
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Close Console"))
					*p_open = false;
				ImGui::EndPopup();
			}

			ImGui::TextWrapped(
				"This example implements a console with basic coloring, completion (TAB key) and history (Up/Down keys). A more elaborate "
				"implementation may want to store entries along with extra data such as timestamp, emitter, etc.");
			ImGui::TextWrapped("Enter 'HELP' for help.");

			// TODO: display items starting from the bottom

			if (ImGui::SmallButton("Add Debug Text")) { AddLog("%d some text", Items.Size); AddLog("some more text"); AddLog("display very important message here!"); }
			ImGui::SameLine();
			if (ImGui::SmallButton("Add Debug Error")) { AddLog("[error] something went wrong"); }
			ImGui::SameLine();
			if (ImGui::SmallButton("Clear")) { ClearLog(); }
			ImGui::SameLine();
			bool copy_to_clipboard = ImGui::SmallButton("Copy");
			//static float t = 0.0f; if (ImGui::GetTime() - t > 0.02f) { t = ImGui::GetTime(); AddLog("Spam %f", t); }

			ImGui::Separator();

			// Options menu
			if (ImGui::BeginPopup("Options"))
			{
				ImGui::Checkbox("Auto-scroll", &AutoScroll);
				ImGui::EndPopup();
			}

			// Options, Filter
			if (ImGui::Button("Options"))
				ImGui::OpenPopup("Options");
			ImGui::SameLine();
			Filter.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
			ImGui::Separator();

			// Reserve enough left-over height for 1 separator + 1 input text
			const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
			ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);
			if (ImGui::BeginPopupContextWindow())
			{
				if (ImGui::Selectable("Clear")) ClearLog();
				ImGui::EndPopup();
			}

			// Display every line as a separate entry so we can change their color or add custom widgets.
			// If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
			// NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
			// to only process visible items. The clipper will automatically measure the height of your first item and then
			// "seek" to display only items in the visible area.
			// To use the clipper we can replace your standard loop:
			//      for (int i = 0; i < Items.Size; i++)
			//   With:
			//      ImGuiListClipper clipper;
			//      clipper.Begin(Items.Size);
			//      while (clipper.Step())
			//         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			// - That your items are evenly spaced (same height)
			// - That you have cheap random access to your elements (you can access them given their index,
			//   without processing all the ones before)
			// You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
			// We would need random-access on the post-filtered list.
			// A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
			// or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
			// and appending newly elements as they are inserted. This is left as a task to the user until we can manage
			// to improve this example code!
			// If your items are of variable height:
			// - Split them into same height items would be simpler and facilitate random-seeking into your list.
			// - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
			if (copy_to_clipboard)
				ImGui::LogToClipboard();
			for (int i = 0; i < Items.Size; i++)
			{
				const char* item = Items[i];
				if (!Filter.PassFilter(item))
					continue;

				// Normally you would store more information in your item than just a string.
				// (e.g. make Items[] an array of structure, store color/type etc.)
				ImVec4 color;
				bool has_color = false;
				if (strstr(item, "[error]")) { color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); has_color = true; }
				else if (strncmp(item, "# ", 2) == 0) { color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f); has_color = true; }
				if (has_color)
					ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::TextUnformatted(item);
				if (has_color)
					ImGui::PopStyleColor();
			}
			if (copy_to_clipboard)
				ImGui::LogFinish();

			if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
				ImGui::SetScrollHereY(1.0f);
			ScrollToBottom = false;

			ImGui::PopStyleVar();
			ImGui::EndChild();
			ImGui::Separator();

			// Command-line
			bool reclaim_focus = false;
			ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
			if (ImGui::InputText("Input", InputBuf, IM_ARRAYSIZE(InputBuf), input_text_flags, &TextEditCallbackStub, (void*)this))
			{
				char* s = InputBuf;
				Strtrim(s);
				if (s[0])
					ExecCommand(s);
				strcpy(s, "");
				reclaim_focus = true;
			}

			// Auto-focus on window apparition
			ImGui::SetItemDefaultFocus();
			if (reclaim_focus)
				ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

			ImGui::End();
		}

		void    ExecCommand(const char* command_line)
		{
			AddLog("# %s\n", command_line);

			// Insert into history. First find match and delete it so it can be pushed to the back.
			// This isn't trying to be smart or optimal.
			HistoryPos = -1;
			for (int i = History.Size - 1; i >= 0; i--)
				if (Stricmp(History[i], command_line) == 0)
				{
					free(History[i]);
					History.erase(History.begin() + i);
					break;
				}
			History.push_back(Strdup(command_line));

			// Process command
			if (Stricmp(command_line, "CLEAR") == 0)
			{
				ClearLog();
			}
			else if (Stricmp(command_line, "HELP") == 0)
			{
				AddLog("Commands:");
				for (int i = 0; i < Commands.Size; i++)
					AddLog("- %s", Commands[i]);
			}
			else if (Stricmp(command_line, "HISTORY") == 0)
			{
				int first = History.Size - 10;
				for (int i = first > 0 ? first : 0; i < History.Size; i++)
					AddLog("%3d: %s\n", i, History[i]);
			}
			else
			{
				AddLog("Unknown command: '%s'\n", command_line);
			}

			ScrollToBottom = true;
		}

		static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
		{
			ImGuiConsole* console = (ImGuiConsole*)data->UserData;
			return console->TextEditCallback(data);
		}

		int     TextEditCallback(ImGuiInputTextCallbackData* data)
		{
			switch (data->EventFlag)
			{
			case ImGuiInputTextFlags_CallbackCompletion:
			{
				// Example of TEXT COMPLETION

				// Locate beginning of current word
				const char* word_end = data->Buf + data->CursorPos;
				const char* word_start = word_end;
				while (word_start > data->Buf)
				{
					const char c = word_start[-1];
					if (c == ' ' || c == '\t' || c == ',' || c == ';')
						break;
					word_start--;
				}

				// Build a list of candidates
				ImVector<const char*> candidates;
				for (int i = 0; i < Commands.Size; i++)
					if (Strnicmp(Commands[i], word_start, (int)(word_end - word_start)) == 0)
						candidates.push_back(Commands[i]);

				if (candidates.Size == 0)
				{
					// No match
					AddLog("No match for \"%.*s\"!\n", (int)(word_end - word_start), word_start);
				}
				else if (candidates.Size == 1)
				{
					// Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
					data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
					data->InsertChars(data->CursorPos, candidates[0]);
					data->InsertChars(data->CursorPos, " ");
				}
				else
				{
					// Multiple matches. Complete as much as we can..
					// So inputing "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
					int match_len = (int)(word_end - word_start);
					for (;;)
					{
						int c = 0;
						bool all_candidates_matches = true;
						for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
							if (i == 0)
								c = toupper(candidates[i][match_len]);
							else if (c == 0 || c != toupper(candidates[i][match_len]))
								all_candidates_matches = false;
						if (!all_candidates_matches)
							break;
						match_len++;
					}

					if (match_len > 0)
					{
						data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
						data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
					}

					// List matches
					AddLog("Possible matches:\n");
					for (int i = 0; i < candidates.Size; i++)
						AddLog("- %s\n", candidates[i]);
				}

				break;
			}
			case ImGuiInputTextFlags_CallbackHistory:
			{
				// Example of HISTORY
				const int prev_history_pos = HistoryPos;
				if (data->EventKey == ImGuiKey_UpArrow)
				{
					if (HistoryPos == -1)
						HistoryPos = History.Size - 1;
					else if (HistoryPos > 0)
						HistoryPos--;
				}
				else if (data->EventKey == ImGuiKey_DownArrow)
				{
					if (HistoryPos != -1)
						if (++HistoryPos >= History.Size)
							HistoryPos = -1;
				}

				// A better implementation would preserve the data on the current input line along with cursor position.
				if (prev_history_pos != HistoryPos)
				{
					const char* history_str = (HistoryPos >= 0) ? History[HistoryPos] : "";
					data->DeleteChars(0, data->BufTextLen);
					data->InsertChars(0, history_str);
				}
			}
			}
			return 0;
		}
	};

	Editor::Editor() = default;
	Editor::~Editor() = default;
	void Editor::Init(EditorInit&& init)
	{
		editor_log = std::make_unique<ImGuiLogger>();
		ADRIA_REGISTER_LOGGER(new EditorLogger(editor_log.get()));
		engine = std::make_unique<Engine>(init.engine_init);
		gui = std::make_unique<GUI>(engine->gfx.get());
		engine->RegisterEditorEventCallbacks(editor_events);
		SetStyle();
	}
	void Editor::Destroy()
	{
		while (!aabb_updates.empty()) aabb_updates.pop();
		gui.reset();
		engine.reset();
		editor_log.reset();
	}
	void Editor::HandleWindowMessage(WindowMessage const& msg_data)
	{
		engine->HandleWindowMessage(msg_data);
		gui->HandleWindowMessage(msg_data);
	}
	void Editor::Run()
	{
		HandleInput();
		renderer_settings.gui_visible = gui->IsVisible();
		if (gui->IsVisible())
		{
			engine->SetViewportData(viewport_data);
			engine->Run(renderer_settings);
			auto gui_cmd_list = engine->gfx->GetDefaultCommandList();
			engine->gfx->SetBackbuffer(gui_cmd_list);
			{
				PIXScopedEvent(gui_cmd_list, PIX_COLOR_DEFAULT, "GUI Pass");
				gui->Begin();
				MenuBar();
				ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
				Scene();
				ListEntities();
				AddEntities();
				Settings();
				Camera();
				Properties();
				Log();
				Profiling();
				ShaderHotReload();
				Debug();
				gui->End(gui_cmd_list);
			}
			if (!aabb_updates.empty())
			{
				engine->gfx->WaitForGPU();
				while (!aabb_updates.empty())
				{
					AABB* aabb = aabb_updates.front();
					aabb->UpdateBuffer(engine->gfx.get());
					aabb_updates.pop();
				}
			}
			engine->Present();
		}
		else
		{
			engine->SetViewportData(std::nullopt);
			engine->Run(renderer_settings);
			engine->Present();
		}

		if (reload_shaders)
		{
			engine->gfx->WaitForGPU();
			ShaderCache::CheckIfShadersHaveChanged();
			reload_shaders = false;
		}
	}
	void Editor::AddCommand(GUICommand&& command)
	{
		commands.emplace_back(std::move(command));
	}

	void Editor::AddDebugCommand(GUICommand_Debug&& command)
	{
		debug_commands.emplace_back(std::move(command));
	}

	void Editor::SetStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();

		style.FrameRounding = 0.0f;
		style.GrabRounding = 1.0f;
		style.WindowRounding = 0.0f;
		style.IndentSpacing = 10.0f;
		style.ScrollbarSize = 16.0f;
		style.WindowPadding = ImVec2(5, 5);
		style.FramePadding = ImVec2(2, 2);

		ImVec4* colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
		colors[ImGuiCol_Border] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.14f, 0.71f, 0.83f, 0.95f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.67f, 0.82f, 0.83f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.42f, 0.80f, 0.96f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.37f, 0.37f, 0.37f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.58f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
		colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.23f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
		colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.37f, 0.37f, 0.37f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_PlotLines] = ImVec4(0.73f, 0.29f, 0.29f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
		colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
		colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
	}
	void Editor::HandleInput()
	{
		if (scene_focused && Input::GetInstance().IsKeyDown(EKeyCode::I)) gui->ToggleVisibility();
		if (scene_focused && Input::GetInstance().IsKeyDown(EKeyCode::G)) gizmo_enabled = !gizmo_enabled;
		if (gizmo_enabled && gui->IsVisible())
		{
			if (Input::GetInstance().IsKeyDown(EKeyCode::T)) gizmo_op = ImGuizmo::TRANSLATE;
			if (Input::GetInstance().IsKeyDown(EKeyCode::R)) gizmo_op = ImGuizmo::ROTATE;
			if (Input::GetInstance().IsKeyDown(EKeyCode::E)) gizmo_op = ImGuizmo::SCALE;
		}
		engine->camera->Enable(scene_focused);
	}
	void Editor::MenuBar()
	{
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Load Model"))
				{
					nfdchar_t* file_path = NULL;
					const nfdchar_t* filter_list = "gltf";
					nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
					if (result == NFD_OKAY)
					{
						std::string model_path = file_path;

						ModelParameters params{};
						params.model_path = model_path;
						std::string texture_path = GetParentPath(model_path);
						if (!texture_path.empty()) texture_path.append("/");

						params.textures_path = texture_path;
						engine->entity_loader->ImportModel_GLTF(params);
						free(file_path);
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Windows"))
			{
				if (ImGui::MenuItem("Profiler", 0, window_flags[Flag_Profiler]))			 window_flags[Flag_Profiler] = !window_flags[Flag_Profiler];
				if (ImGui::MenuItem("Log", 0, window_flags[Flag_Log]))						 window_flags[Flag_Log] = !window_flags[Flag_Log];
				if (ImGui::MenuItem("Camera", 0, window_flags[Flag_Camera]))				 window_flags[Flag_Camera] = !window_flags[Flag_Camera];
				if (ImGui::MenuItem("Entities", 0, window_flags[Flag_Entities]))			 window_flags[Flag_Entities] = !window_flags[Flag_Entities];
				if (ImGui::MenuItem("Hot Reload", 0, window_flags[Flag_HotReload]))			 window_flags[Flag_HotReload] = !window_flags[Flag_HotReload];
				if (ImGui::MenuItem("Settings", 0, window_flags[Flag_Settings]))			 window_flags[Flag_Settings] = !window_flags[Flag_Settings];
				if (ImGui::MenuItem("Debug", 0, window_flags[Flag_Debug]))					 window_flags[Flag_Debug] = !window_flags[Flag_Debug];
				if (ImGui::MenuItem("Add Entities", 0, window_flags[Flag_AddEntities]))		 window_flags[Flag_AddEntities] = !window_flags[Flag_AddEntities];

				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Help"))
			{
				ImGui::Text("Controls\n");
				ImGui::Text(
					"Move Camera with W, A, S, D, Q and E. Hold Right Click and move Mouse for rotating Camera. Use Mouse Scroll for Zoom In/Out.\n"
					"Press I to toggle between Cinema Mode and Editor Mode. (Scene Window has to be active) \n"
					"Press G to toggle Gizmo. (Scene Window has to be active) \n"
					"When Gizmo is enabled, use T, R and E to switch between Translation, Rotation and Scaling Mode.\n"
					"To hot-reload shaders, press F5."
				);
				ImGui::Spacing();

				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
	}

	void Editor::AddEntities()
	{
		if (!window_flags[Flag_AddEntities]) return;
		if (ImGui::Begin("Add Entities", &window_flags[Flag_AddEntities]))
		{
			if (ImGui::TreeNodeEx("Point Lights", 0))
			{
				ImGui::Text("For Easy Demonstration of Tiled Deferred Rendering");
				static int light_count_to_add = 1;
				ImGui::SliderInt("Light Count", &light_count_to_add, 1, 128);
				if (ImGui::Button("Create Random Point Lights"))
				{
					static RealRandomGenerator real(0.0f, 1.0f);

					for (int32 i = 0; i < light_count_to_add; ++i)
					{
						LightParameters light_params{};
						light_params.light_data.casts_shadows = false;
						light_params.light_data.color = DirectX::XMVectorSet(real() * 2, real() * 2, real() * 2, 1.0f);
						light_params.light_data.direction = DirectX::XMVectorSet(0.5f, -1.0f, 0.1f, 0.0f);
						light_params.light_data.position = DirectX::XMVectorSet(real() * 200 - 100, real() * 200.0f, real() * 200 - 100, 1.0f);
						light_params.light_data.type = ELightType::Point;
						light_params.mesh_type = ELightMesh::NoMesh;
						light_params.light_data.range = real() * 100.0f + 40.0f;
						light_params.light_data.active = true;
						light_params.light_data.volumetric = false;
						light_params.light_data.volumetric_strength = 0.3f;
						engine->entity_loader->LoadLight(light_params);
					}
				}
				ImGui::TreePop();
				ImGui::Separator();
			}
			if (ImGui::TreeNodeEx("Spot Lights", 0))
			{
				static int light_count_to_add = 1;
				ImGui::SliderInt("Light Count", &light_count_to_add, 1, 128);
				if (ImGui::Button("Create Random Spot Lights"))
				{
					static RealRandomGenerator real(0.0f, 1.0f);

					for (int32 i = 0; i < light_count_to_add; ++i)
					{
						LightParameters light_params{};
						light_params.light_data.casts_shadows = false;
						light_params.light_data.inner_cosine = real();
						light_params.light_data.outer_cosine = real();
						light_params.light_data.color = DirectX::XMVectorSet(real() * 2, real() * 2, real() * 2, 1.0f);
						light_params.light_data.direction = DirectX::XMVectorSet(0.5f, -1.0f, 0.1f, 0.0f);
						light_params.light_data.position = DirectX::XMVectorSet(real() * 200 - 100, real() * 200.0f, real() * 200 - 100, 1.0f);
						light_params.light_data.type = ELightType::Spot;
						light_params.mesh_type = ELightMesh::NoMesh;
						light_params.light_data.range = real() * 100.0f + 40.0f;
						light_params.light_data.active = true;
						light_params.light_data.volumetric = false;
						light_params.light_data.volumetric_strength = 0.3f;
						if (light_params.light_data.inner_cosine > light_params.light_data.outer_cosine)
							std::swap(light_params.light_data.inner_cosine, light_params.light_data.outer_cosine);
						engine->entity_loader->LoadLight(light_params);
					}
				}
				ImGui::TreePop();
				ImGui::Separator();
			}
			if (ImGui::TreeNodeEx("Ocean", 0))
			{
				static GridParameters ocean_params{};
				static int32 tile_count[2] = { 512, 512 };
				static float tile_size[2] = { 40.0f, 40.0f };
				static float texture_scale[2] = { 20.0f, 20.0f };

				ImGui::SliderInt2("Tile Count", tile_count, 32, 1024);
				ImGui::SliderFloat2("Tile Size", tile_size, 1.0, 100.0f);
				ImGui::SliderFloat2("Texture Scale", texture_scale, 0.1f, 10.0f);

				ocean_params.tile_count_x = tile_count[0];
				ocean_params.tile_count_z = tile_count[1];
				ocean_params.tile_size_x = tile_size[0];
				ocean_params.tile_size_z = tile_size[1];
				ocean_params.texture_scale_x = texture_scale[0];
				ocean_params.texture_scale_z = texture_scale[1];

				if (ImGui::Button("Load Ocean"))
				{
					OceanParameters params{};
					params.ocean_grid = std::move(ocean_params);
					engine->entity_loader->LoadOcean(params);
				}

				if (ImGui::Button("Clear"))
				{
					engine->reg.clear<Ocean>();
				}
				ImGui::TreePop();
				ImGui::Separator();
			}
			if (ImGui::TreeNodeEx("Decals", 0))
			{
				static DecalParameters params{};
				static char NAME_BUFFER[128];
				ImGui::InputText("Name", NAME_BUFFER, sizeof(NAME_BUFFER));
				params.name = std::string(NAME_BUFFER);
				ImGui::PushID(6);
				if (ImGui::Button("Select Albedo Texture"))
				{
					nfdchar_t* file_path = NULL;
					nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
					nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
					if (result == NFD_OKAY)
					{
						std::string texture_path = file_path;
						params.albedo_texture_path = texture_path;
						free(file_path);
					}
				}
				ImGui::PopID();
				ImGui::Text(params.albedo_texture_path.c_str());

				ImGui::PushID(7);
				if (ImGui::Button("Select Normal Texture"))
				{
					nfdchar_t* file_path = NULL;
					nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
					nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
					if (result == NFD_OKAY)
					{
						std::string texture_path = file_path;
						params.normal_texture_path = texture_path;
						free(file_path);
					}
				}

				ImGui::PopID();
				ImGui::Text(params.normal_texture_path.c_str());

				ImGui::DragFloat("Size", &params.size, 2.0f, 10.0f, 200.0f);
				ImGui::DragFloat("Rotation", &params.rotation, 1.0f, -180.0f, 180.0f);
				ImGui::Checkbox("Modify GBuffer Normals", &params.modify_gbuffer_normals);

				auto picking_data = engine->renderer->GetPickingData();
				ImGui::Text("Picked Position: %f %f %f", picking_data.position.x, picking_data.position.y, picking_data.position.z);
				ImGui::Text("Picked Normal: %f %f %f", picking_data.normal.x, picking_data.normal.y, picking_data.normal.z);
				if (ImGui::Button("Load Decal"))
				{
					params.position = picking_data.position;
					params.normal = picking_data.normal;
					params.rotation = XMConvertToRadians(params.rotation);

					engine->entity_loader->LoadDecal(params);
				}
				if (ImGui::Button("Clear Decals"))
				{
					for (auto e : engine->reg.view<Decal>()) engine->reg.destroy(e);
				}
				ImGui::TreePop();
				ImGui::Separator();
			}
		}
		ImGui::End();
	}
	void Editor::ListEntities()
	{
		if (!window_flags[Flag_Entities]) return;
		auto all_entities = engine->reg.view<Tag>();
		if (ImGui::Begin("Entities", &window_flags[Flag_Entities]))
		{
			std::vector<entt::entity> deleted_entities{};
			std::function<void(entt::entity, bool)> ShowEntity;
			ShowEntity = [&](entt::entity e, bool first_iteration)
			{
				Relationship* relationship = engine->reg.try_get<Relationship>(e);
				if (first_iteration && relationship && relationship->parent != entt::null) return;
				auto& tag = all_entities.get<Tag>(e);

				ImGuiTreeNodeFlags flags = ((selected_entity == e) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
				flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
				bool opened = ImGui::TreeNodeEx(tag.name.c_str(), flags);

				if (ImGui::IsItemClicked())
				{
					if (e == selected_entity) selected_entity = entt::null;
					else selected_entity = e;
				}

				if (opened)
				{
					if (relationship)
					{
						for (size_t i = 0; i < relationship->children_count; ++i)
						{
							ShowEntity(relationship->children[i], false);
						}
					}
					ImGui::TreePop();
				}
			};
			for (auto e : all_entities) ShowEntity(e, true);
		}
		ImGui::End();
	}
	void Editor::Properties()
	{
		if (!window_flags[Flag_Entities]) return;
		if (ImGui::Begin("Properties", &window_flags[Flag_Entities]))
		{
			if (selected_entity != entt::null)
			{
				auto tag = engine->reg.try_get<Tag>(selected_entity);
				if (tag)
				{
					char buffer[256];
					memset(buffer, 0, sizeof(buffer));
					std::strncpy(buffer, tag->name.c_str(), sizeof(buffer));
					if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
						tag->name = std::string(buffer);
				}

				auto light = engine->reg.try_get<Light>(selected_entity);
				if (light && ImGui::CollapsingHeader("Light"))
				{
					if (light->type == ELightType::Directional)	ImGui::Text("Directional Light");
					else if (light->type == ELightType::Spot)	ImGui::Text("Spot Light");
					else if (light->type == ELightType::Point)	ImGui::Text("Point Light");

					XMFLOAT4 light_color, light_direction, light_position;
					XMStoreFloat4(&light_color, light->color);
					XMStoreFloat4(&light_direction, light->direction);
					XMStoreFloat4(&light_position, light->position);

					float color[3] = { light_color.x, light_color.y, light_color.z };
					ImGui::ColorEdit3("Light Color", color);
					light->color = XMVectorSet(color[0], color[1], color[2], 1.0f);

					ImGui::SliderFloat("Light Energy", &light->energy, 0.0f, 50.0f);

					if (engine->reg.all_of<Material>(selected_entity))
					{
						auto& material = engine->reg.get<Material>(selected_entity);
						memcpy(material.base_color, color, 3 * sizeof(float));
					}

					if (light->type == ELightType::Directional || light->type == ELightType::Spot)
					{
						float direction[3] = { light_direction.x, light_direction.y, light_direction.z };
						ImGui::SliderFloat3("Light direction", direction, -1.0f, 1.0f);
						light->direction = XMVectorSet(direction[0], direction[1], direction[2], 0.0f);
						if (light->type == ELightType::Directional)
						{
							light->position = XMVectorScale(-light->direction, 1e3);
						}
					}

					if (light->type == ELightType::Spot)
					{
						float inner_angle = XMConvertToDegrees(acos(light->inner_cosine))
							, outer_angle = XMConvertToDegrees(acos(light->outer_cosine));
						ImGui::SliderFloat("Inner Spot Angle", &inner_angle, 0.0f, 90.0f);
						ImGui::SliderFloat("Outer Spot Angle", &outer_angle, inner_angle, 90.0f);

						light->inner_cosine = cos(XMConvertToRadians(inner_angle));
						light->outer_cosine = cos(XMConvertToRadians(outer_angle));
					}

					if (light->type == ELightType::Point || light->type == ELightType::Spot)
					{
						float position[3] = { light_position.x, light_position.y, light_position.z };

						ImGui::SliderFloat3("Light position", position, -300.0f, 500.0f);

						light->position = XMVectorSet(position[0], position[1], position[2], 1.0f);

						ImGui::SliderFloat("Range", &light->range, 50.0f, 1000.0f);
					}

					if (engine->reg.all_of<Transform>(selected_entity))
					{
						auto& tr = engine->reg.get<Transform>(selected_entity);

						tr.current_transform = XMMatrixTranslationFromVector(light->position);
					}

					ImGui::Checkbox("Active", &light->active);

					if (light->type == ELightType::Directional)
					{
						const char* shadow_types[] = { "None", "Shadow Maps", "Ray Traced Shadows" };
						static int current_shadow_type = light->casts_shadows;
						const char* combo_label = shadow_types[current_shadow_type];
						if (ImGui::BeginCombo("Shadows Type", combo_label, 0))
						{
							for (int n = 0; n < IM_ARRAYSIZE(shadow_types); n++)
							{
								const bool is_selected = (current_shadow_type == n);
								if (ImGui::Selectable(shadow_types[n], is_selected)) current_shadow_type = n;
								if (is_selected) ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
						if (!engine->renderer->IsRayTracingSupported() && current_shadow_type == 2)
						{
							current_shadow_type = 1;
						}
						light->casts_shadows = (current_shadow_type == 1);
						light->ray_traced_shadows = (current_shadow_type == 2);
					}
					else
					{
						ImGui::Checkbox("Casts Shadows", &light->casts_shadows);
					}

					if (light->casts_shadows)
					{
						if (light->type == ELightType::Directional && light->casts_shadows)
						{
							bool use_cascades = static_cast<bool>(light->use_cascades);
							ImGui::Checkbox("Use Cascades", &use_cascades);
							light->use_cascades = use_cascades;
						}
						ImGui::Checkbox("Screen Space Contact Shadows", &light->sscs);
						if (light->sscs)
						{
							ImGui::SliderFloat("Thickness", &light->sscs_thickness, 0.0f, 1.0f);
							ImGui::SliderFloat("Max Ray Distance", &light->sscs_max_ray_distance, 0.0f, 0.3f);
							ImGui::SliderFloat("Max Depth Distance", &light->sscs_max_depth_distance, 0.0f, 500.0f);
						}
					}
					else if (light->ray_traced_shadows)
					{
						ImGui::Checkbox("Soft Shadows", &light->soft_rts);
					}

					ImGui::Checkbox("God Rays", &light->god_rays);
					if (light->god_rays)
					{
						ImGui::SliderFloat("God Rays decay", &light->godrays_decay, 0.0f, 1.0f);
						ImGui::SliderFloat("God Rays weight", &light->godrays_weight, 0.0f, 1.0f);
						ImGui::SliderFloat("God Rays density", &light->godrays_density, 0.1f, 2.0f);
						ImGui::SliderFloat("God Rays exposure", &light->godrays_exposure, 0.1f, 10.0f);
					}

					ImGui::Checkbox("Volumetric Lighting", &light->volumetric);
					if (light->volumetric)
					{
						ImGui::SliderFloat("Volumetric lighting Strength", &light->volumetric_strength, 0.0f, 5.0f);
					}

					ImGui::Checkbox("Lens Flare", &light->lens_flare);
				}

				auto material = engine->reg.try_get<Material>(selected_entity);
				if (material && ImGui::CollapsingHeader("Material"))
				{
					ID3D12Device5* device = engine->gfx->GetDevice();
					RingOnlineDescriptorAllocator* descriptor_allocator = gui->DescriptorAllocator();

					ImGui::Text("Albedo Texture");
					D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetTextureManager().GetSRV(material->albedo_texture);
					OffsetType descriptor_index = descriptor_allocator->Allocate();
					auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(0);
					if (ImGui::Button("Remove")) material->albedo_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->albedo_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::Text("Metallic-Roughness Texture");
					tex_handle = engine->renderer->GetTextureManager().GetSRV(material->metallic_roughness_texture);
					descriptor_index = descriptor_allocator->Allocate();
					dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(1);
					if (ImGui::Button("Remove")) material->metallic_roughness_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->metallic_roughness_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::Text("Emissive Texture");
					tex_handle = engine->renderer->GetTextureManager().GetSRV(material->emissive_texture);
					descriptor_index = descriptor_allocator->Allocate();
					dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(2);
					if (ImGui::Button("Remove")) material->emissive_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->emissive_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::ColorEdit3("Base Color", material->base_color);
					ImGui::SliderFloat("Metallic Factor", &material->metallic_factor, 0.0f, 1.0f);
					ImGui::SliderFloat("Roughness Factor", &material->roughness_factor, 0.0f, 1.0f);
					ImGui::SliderFloat("Emissive Factor", &material->emissive_factor, 0.0f, 32.0f);

					material->pso = EPipelineState::GBuffer;
				}

				auto transform = engine->reg.try_get<Transform>(selected_entity);
				if (transform && ImGui::CollapsingHeader("Transform"))
				{
					XMFLOAT4X4 tr;
					XMStoreFloat4x4(&tr, transform->current_transform);

					float translation[3], rotation[3], scale[3];
					ImGuizmo::DecomposeMatrixToComponents(tr.m[0], translation, rotation, scale);
					bool change = ImGui::InputFloat3("Translation", translation);
					change &= ImGui::InputFloat3("Rotation", rotation);
					change &= ImGui::InputFloat3("Scale", scale);
					ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, tr.m[0]);

					if (AABB* aabb = engine->reg.try_get<AABB>(selected_entity))
					{
						aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMMatrixInverse(nullptr, transform->current_transform));
						aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMLoadFloat4x4(&tr));
						if(change) aabb_updates.push(aabb);
					}

					if (Relationship* relationship = engine->reg.try_get<Relationship>(selected_entity))
					{
						for (size_t i = 0; i < relationship->children_count; ++i)
						{
							entt::entity child = relationship->children[i];
							if (AABB* aabb = engine->reg.try_get<AABB>(child))
							{
								aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMMatrixInverse(nullptr, transform->current_transform));
								aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMLoadFloat4x4(&tr));
								if (change) aabb_updates.push(aabb);
							}
						}
					}
					transform->current_transform = DirectX::XMLoadFloat4x4(&tr);
				}

				auto decal = engine->reg.try_get<Decal>(selected_entity);
				if (decal && ImGui::CollapsingHeader("Decal"))
				{
					ID3D12Device5* device = engine->gfx->GetDevice();
					RingOnlineDescriptorAllocator* descriptor_allocator = gui->DescriptorAllocator();

					ImGui::Text("Decal Albedo Texture");
					D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetTextureManager().GetSRV(decal->albedo_decal_texture);
					OffsetType descriptor_index = descriptor_allocator->Allocate();
					auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(4);
					if (ImGui::Button("Remove")) decal->albedo_decal_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							decal->albedo_decal_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::Text("Decal Normal Texture");
					tex_handle = engine->renderer->GetTextureManager().GetSRV(decal->normal_decal_texture);
					descriptor_index = descriptor_allocator->Allocate();
					dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(5);
					if (ImGui::Button("Remove")) decal->normal_decal_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							decal->normal_decal_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();
					ImGui::Checkbox("Modify GBuffer Normals", &decal->modify_gbuffer_normals);
				}

				auto skybox = engine->reg.try_get<Skybox>(selected_entity);
				if (skybox && ImGui::CollapsingHeader("Skybox"))
				{
					ImGui::Checkbox("Active", &skybox->active);
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							skybox->cubemap_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
				}

				auto forward = engine->reg.try_get<Forward>(selected_entity);
				if (forward)
				{
					if (ImGui::CollapsingHeader("Forward")) ImGui::Checkbox("Transparent", &forward->transparent);
				}

				if (AABB* aabb = engine->reg.try_get<AABB>(selected_entity))
				{
					aabb->draw_aabb = true;
				}
			}
		}
		ImGui::End();
	}
	void Editor::Camera()
	{
		if (!window_flags[Flag_Camera]) return;
		
		auto& camera = *engine->camera;
		if (ImGui::Begin("Camera", &window_flags[Flag_Camera]))
		{
			XMFLOAT3 cam_pos;
			XMStoreFloat3(&cam_pos, camera.Position());
			float pos[3] = { cam_pos.x , cam_pos.y, cam_pos.z };
			ImGui::SliderFloat3("Position", pos, 0.0f, 2000.0f);
			camera.SetPosition(DirectX::XMFLOAT3(pos));
			float near_plane = camera.Near(), far_plane = camera.Far();
			float _fov = camera.Fov(), _ar = camera.AspectRatio();
			ImGui::SliderFloat("Near", &near_plane, 0.0f, 2.0f);
			ImGui::SliderFloat("Far", &far_plane, 10.0f, 3000.0f);
			ImGui::SliderFloat("FOV", &_fov, 0.01f, 1.5707f);
			camera.SetNearAndFar(near_plane, far_plane);
			camera.SetFov(_fov);
		}
		ImGui::End();
	}
	void Editor::Scene()
	{
		ImGui::Begin("Scene");
		{
			auto device = engine->gfx->GetDevice();
			auto descriptor_allocator = gui->DescriptorAllocator();

			ImVec2 v_min = ImGui::GetWindowContentRegionMin();
			ImVec2 v_max = ImGui::GetWindowContentRegionMax();
			v_min.x += ImGui::GetWindowPos().x;
			v_min.y += ImGui::GetWindowPos().y;
			v_max.x += ImGui::GetWindowPos().x;
			v_max.y += ImGui::GetWindowPos().y;
			ImVec2 size(v_max.x - v_min.x, v_max.y - v_min.y);

			D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetFinalTexture()->GetSRV();
			OffsetType descriptor_index = descriptor_allocator->Allocate();
			auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
			device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr, size);

			scene_focused = ImGui::IsWindowFocused();

			ImVec2 mouse_pos = ImGui::GetMousePos();
			viewport_data.mouse_position_x = mouse_pos.x;
			viewport_data.mouse_position_y = mouse_pos.y;
			viewport_data.scene_viewport_focused = scene_focused;
			viewport_data.scene_viewport_pos_x = v_min.x;
			viewport_data.scene_viewport_pos_y = v_min.y;
			viewport_data.scene_viewport_size_x = size.x;
			viewport_data.scene_viewport_size_y = size.y;
		}

		if (selected_entity != entt::null && engine->reg.all_of<Transform>(selected_entity) && gizmo_enabled)
		{
			ImGuizmo::SetDrawlist();

			ImVec2 window_size = ImGui::GetWindowSize();
			ImVec2 window_pos = ImGui::GetWindowPos();

			ImGuizmo::SetRect(window_pos.x, window_pos.y,
				window_size.x, window_size.y);

			auto& camera = *engine->camera;

			auto camera_view = camera.View();
			auto camera_proj = camera.Proj();

			DirectX::XMFLOAT4X4 view, projection;

			DirectX::XMStoreFloat4x4(&view, camera_view);
			DirectX::XMStoreFloat4x4(&projection, camera_proj);

			auto& entity_transform = engine->reg.get<Transform>(selected_entity);

			DirectX::XMFLOAT4X4 tr;
			DirectX::XMStoreFloat4x4(&tr, entity_transform.current_transform);

			bool change = ImGuizmo::Manipulate(view.m[0], projection.m[0], gizmo_op, ImGuizmo::LOCAL,
				tr.m[0]);

			if (ImGuizmo::IsUsing())
			{
				AABB* aabb = engine->reg.try_get<AABB>(selected_entity);
				if (aabb)
				{
					aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMMatrixInverse(nullptr, entity_transform.current_transform));
					aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMLoadFloat4x4(&tr));
					if(change) aabb_updates.push(aabb);
				}

				if (Relationship* relationship = engine->reg.try_get<Relationship>(selected_entity))
				{
					for (size_t i = 0; i < relationship->children_count; ++i)
					{
						entt::entity child = relationship->children[i];
						if (AABB* aabb = engine->reg.try_get<AABB>(child))
						{
							aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMMatrixInverse(nullptr, entity_transform.current_transform));
							aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMLoadFloat4x4(&tr));
							if (change) aabb_updates.push(aabb);
						}
					}
				}
				entity_transform.current_transform = DirectX::XMLoadFloat4x4(&tr);
			}
		}

		ImGui::End();
	}
	void Editor::Log()
	{
		if (!window_flags[Flag_Log]) return;
		if(ImGui::Begin("Log", &window_flags[Flag_Log]))
		{
			editor_log->Draw("Log");
		}
		ImGui::End();
	}
	void Editor::Settings()
	{
		if (!window_flags[Flag_Settings]) return;
		if (ImGui::Begin("Settings", &window_flags[Flag_Settings]))
		{
			static const char* render_path_types[] = { "Regular Deferred", "Tiled Deferred", "Clustered Deferred", "Path Tracing" };
			static int current_render_path_type = 0;
			const char* render_path_combo_label = render_path_types[current_render_path_type];
			if (ImGui::BeginCombo("Render Path", render_path_combo_label, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(render_path_types); n++)
				{
					const bool is_selected = (current_render_path_type == n);
					if (ImGui::Selectable(render_path_types[n], is_selected)) current_render_path_type = n;
					if (is_selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (!engine->renderer->IsRayTracingSupported() && current_render_path_type == 3)
			{
				current_render_path_type = 0;
			}
			renderer_settings.render_path = static_cast<ERenderPathType>(current_render_path_type);

			static const char* ao_types[] = { "None", "SSAO", "HBAO", "RTAO" };
			static int current_ao_type = 0;
			const char* ao_combo_label = ao_types[current_ao_type];
			if (ImGui::BeginCombo("Ambient Occlusion", ao_combo_label, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(ao_types); n++)
				{
					const bool is_selected = (current_ao_type == n);
					if (ImGui::Selectable(ao_types[n], is_selected)) current_ao_type = n;
					if (is_selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (!engine->renderer->IsRayTracingSupported() && current_ao_type == 3)
			{
				current_ao_type = 1;
			}
			renderer_settings.postprocess.ambient_occlusion = static_cast<EAmbientOcclusion>(current_ao_type);
			
			static const char* reflection_types[] = { "None", "SSR", "RTR" };
			static int current_reflection_type = (int)renderer_settings.postprocess.reflections;
			const char* reflection_combo_label = reflection_types[current_reflection_type];
			if (ImGui::BeginCombo("Reflections", reflection_combo_label, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(reflection_types); n++)
				{
					const bool is_selected = (current_reflection_type == n);
					if (ImGui::Selectable(reflection_types[n], is_selected)) current_reflection_type = n;
					if (is_selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (!engine->renderer->IsRayTracingSupported() && current_reflection_type == 2)
			{
				current_reflection_type = 1;
			}
			renderer_settings.postprocess.reflections = static_cast<EReflections>(current_reflection_type);
			ImGui::Checkbox("Automatic Exposure", &renderer_settings.postprocess.automatic_exposure);
			ImGui::Checkbox("Volumetric Clouds", &renderer_settings.postprocess.clouds);
			ImGui::Checkbox("DoF", &renderer_settings.postprocess.dof);
			if (renderer_settings.postprocess.dof)
			{
				ImGui::Checkbox("Bokeh", &renderer_settings.postprocess.bokeh);
			}
			ImGui::Checkbox("Bloom", &renderer_settings.postprocess.bloom);
			ImGui::Checkbox("Motion Blur", &renderer_settings.postprocess.motion_blur);
			ImGui::Checkbox("Fog", &renderer_settings.postprocess.fog);
			if (ImGui::TreeNode("Anti-Aliasing"))
			{
				static bool fxaa = false, taa = false;
				ImGui::Checkbox("FXAA", &fxaa);
				ImGui::Checkbox("TAA", &taa);
				if (fxaa) renderer_settings.postprocess.anti_aliasing = static_cast<EAntiAliasing>(renderer_settings.postprocess.anti_aliasing | AntiAliasing_FXAA);
				else renderer_settings.postprocess.anti_aliasing = static_cast<EAntiAliasing>(renderer_settings.postprocess.anti_aliasing & (~AntiAliasing_FXAA));

				if (taa) renderer_settings.postprocess.anti_aliasing = static_cast<EAntiAliasing>(renderer_settings.postprocess.anti_aliasing | AntiAliasing_TAA);
				else renderer_settings.postprocess.anti_aliasing = static_cast<EAntiAliasing>(renderer_settings.postprocess.anti_aliasing & (~AntiAliasing_TAA));

				ImGui::TreePop();
			}

			for (auto&& cmd : commands) cmd.callback();
			commands.clear();
		}
		ImGui::End();
	}
	void Editor::Profiling()
	{
		if (!window_flags[Flag_Profiler]) return;
		if (ImGui::Begin("Profiling", &window_flags[Flag_Profiler]))
		{
			ImGuiIO io = ImGui::GetIO();
			static bool show_profiling = false;
			ImGui::Checkbox("Show Profiling Results", &show_profiling);
			if (show_profiling)
			{
				static ProfilerState state;
				static constexpr uint64 NUM_FRAMES = 128;
				static float FRAME_TIME_ARRAY[NUM_FRAMES] = { 0 };
				static float RECENT_HIGHEST_FRAME_TIME = 0.0f;
				static constexpr int32 FRAME_TIME_GRAPH_MAX_FPS[] = { 800, 240, 120, 90, 65, 45, 30, 15, 10, 5, 4, 3, 2, 1 };
				static float FRAME_TIME_GRAPH_MAX_VALUES[ARRAYSIZE(FRAME_TIME_GRAPH_MAX_FPS)] = { 0 };
				for (uint64 i = 0; i < ARRAYSIZE(FRAME_TIME_GRAPH_MAX_FPS); ++i) { FRAME_TIME_GRAPH_MAX_VALUES[i] = 1000.f / FRAME_TIME_GRAPH_MAX_FPS[i]; }

				std::vector<Timestamp> time_stamps = GPUProfiler::Get().GetProfilerResults(engine->gfx->GetLastGraphicsCommandList());
				FRAME_TIME_ARRAY[NUM_FRAMES - 1] = 1000.0f / io.Framerate;
				for (uint32 i = 0; i < NUM_FRAMES - 1; i++) FRAME_TIME_ARRAY[i] = FRAME_TIME_ARRAY[i + 1];
				RECENT_HIGHEST_FRAME_TIME = std::max(RECENT_HIGHEST_FRAME_TIME, FRAME_TIME_ARRAY[NUM_FRAMES - 1]);
				float frame_time_ms = FRAME_TIME_ARRAY[NUM_FRAMES - 1];
				const int32 fps = static_cast<int32>(1000.0f / frame_time_ms);

				ImGui::Text("FPS        : %d (%.2f ms)", fps, frame_time_ms);
				if (ImGui::CollapsingHeader("Timings", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Show Avg/Min/Max", &state.show_average);
					ImGui::Spacing();

					uint64 i_max = 0;
					for (uint64 i = 0; i < ARRAYSIZE(FRAME_TIME_GRAPH_MAX_VALUES); ++i)
					{
						if (RECENT_HIGHEST_FRAME_TIME < FRAME_TIME_GRAPH_MAX_VALUES[i]) // FRAME_TIME_GRAPH_MAX_VALUES are in increasing order
						{
							i_max = std::min(ARRAYSIZE(FRAME_TIME_GRAPH_MAX_VALUES) - 1, i + 1);
							break;
						}
					}
					ImGui::PlotLines("", FRAME_TIME_ARRAY, NUM_FRAMES, 0, "GPU frame time (ms)", 0.0f, FRAME_TIME_GRAPH_MAX_VALUES[i_max], ImVec2(0, 80));

					constexpr uint32_t avg_timestamp_update_interval = 1000;
					static auto MillisecondsNow = []()
					{
						static LARGE_INTEGER s_frequency;
						static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
						double milliseconds = 0;
						if (s_use_qpc)
						{
							LARGE_INTEGER now;
							QueryPerformanceCounter(&now);
							milliseconds = double(1000.0 * now.QuadPart) / s_frequency.QuadPart;
						}
						else milliseconds = double(GetTickCount64());
						return milliseconds;
					};
					const double current_time = MillisecondsNow();

					bool reset_accumulating_state = false;
					if ((state.accumulating_frame_count > 1) &&
						((current_time - state.last_reset_time) > avg_timestamp_update_interval))
					{
						std::swap(state.displayed_timestamps, state.accumulating_timestamps);
						for (uint32_t i = 0; i < state.displayed_timestamps.size(); i++)
						{
							state.displayed_timestamps[i].sum /= state.accumulating_frame_count;
						}
						reset_accumulating_state = true;
					}

					reset_accumulating_state |= (state.accumulating_timestamps.size() != time_stamps.size());
					if (reset_accumulating_state)
					{
						state.accumulating_timestamps.resize(0);
						state.accumulating_timestamps.resize(time_stamps.size());
						state.last_reset_time = current_time;
						state.accumulating_frame_count = 0;
					}

					FLOAT total_time_ms = 0.0f;
					for (uint64 i = 0; i < time_stamps.size(); i++)
					{
						float value = time_stamps[i].time_in_ms;
						char const* pStrUnit = "ms";
						ImGui::Text("%-18s: %7.2f %s", time_stamps[i].name.c_str(), value, pStrUnit);
						if (state.show_average)
						{
							if (state.displayed_timestamps.size() == time_stamps.size())
							{
								ImGui::SameLine();
								ImGui::Text("  avg: %7.2f %s", state.displayed_timestamps[i].sum, pStrUnit);
								ImGui::SameLine();
								ImGui::Text("  min: %7.2f %s", state.displayed_timestamps[i].minimum, pStrUnit);
								ImGui::SameLine();
								ImGui::Text("  max: %7.2f %s", state.displayed_timestamps[i].maximum, pStrUnit);
							}

							ProfilerState::AccumulatedTimeStamp* pAccumulatingTimeStamp = &state.accumulating_timestamps[i];
							pAccumulatingTimeStamp->sum += time_stamps[i].time_in_ms;
							pAccumulatingTimeStamp->minimum = std::min<float>(pAccumulatingTimeStamp->minimum, time_stamps[i].time_in_ms);
							pAccumulatingTimeStamp->maximum = std::max<float>(pAccumulatingTimeStamp->maximum, time_stamps[i].time_in_ms);
						}
						total_time_ms += value;
					}
					ImGui::Text("Total: %7.2f %s", total_time_ms, "ms");
					state.accumulating_frame_count++;
				}
			}
			static bool display_vram_usage = false;
			ImGui::Checkbox("Display VRAM Usage", &display_vram_usage);
			if (display_vram_usage)
			{
				GPUMemoryUsage vram = engine->gfx->GetMemoryUsage();
				float const ratio = vram.usage * 1.0f / vram.budget;
				std::string vram_display_string = "VRAM usage: " + std::to_string(vram.usage / 1024 / 1024) + "MB / " + std::to_string(vram.budget / 1024 / 1024) + "MB\n";
				if (ratio >= 0.9f && ratio <= 1.0f) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
				else if (ratio > 1.0f) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
				else ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
				ImGui::TextWrapped(vram_display_string.c_str());
				ImGui::PopStyleColor();
			}
		}
		ImGui::End();
	}
	void Editor::ShaderHotReload()
	{
		if (!window_flags[Flag_HotReload]) return;
		if (ImGui::Begin("Shader Hot Reload", &window_flags[Flag_HotReload]))
		{
			if (ImGui::Button("Compile Changed Shaders"))
			{
				reload_shaders = true;
			}
		}
		ImGui::End();
	}
	void Editor::Debug()
	{
		if (!window_flags[Flag_Debug]) return;
		if(ImGui::Begin("Debug", &window_flags[Flag_Debug]))
		{
			for (auto& cmd : debug_commands) cmd.callback(gui->DescriptorAllocator());
			debug_commands.clear();
		}
		ImGui::End();
	}
}
