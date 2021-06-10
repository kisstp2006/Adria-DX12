#define Skybox_RS  "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                  "CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
                  "CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
                  "DescriptorTable(SRV(t0, numDescriptors = 1, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
                  "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL)"

#define ToneMap_RS  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                              "CBV(b5, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "DescriptorTable(SRV(t0, numDescriptors = 1, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
                               "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )"

#define Fxaa_RS  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                              "DescriptorTable(SRV(t0, numDescriptors = 1, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
                               "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )"

#define Taa_RS                  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                "DescriptorTable(SRV(t0, numDescriptors = 2, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
                                "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )"


#define GeometryPassPBR_RS      "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                                "CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
                                "CBV(b4, visibility=SHADER_VISIBILITY_PIXEL), " \
                                "DescriptorTable(SRV(t0, numDescriptors = 4, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
                                "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL)"



#define AmbientPBR_RS     "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                              "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "DescriptorTable(SRV(t0, numDescriptors = 4, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                              "DescriptorTable(SRV(t7, numDescriptors = 4, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                              "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )"

#define LightingPBR_RS     "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                              "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "CBV(b3, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "DescriptorTable(SRV(t0, numDescriptors = 3, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                              "DescriptorTable(SRV(t4, numDescriptors = 3, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                              "StaticSampler(s0, " \
                                    "addressU = TEXTURE_ADDRESS_WRAP, " \
                                    "addressV = TEXTURE_ADDRESS_WRAP, " \
                                    "addressW = TEXTURE_ADDRESS_WRAP, " \
                                    "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )," \
                              "StaticSampler(s1, " \
                                    "addressU = TEXTURE_ADDRESS_BORDER, " \
                                    "addressV = TEXTURE_ADDRESS_BORDER, " \
                                    "addressW = TEXTURE_ADDRESS_BORDER, " \
                                    "filter = FILTER_COMPARISON_MIN_MAG_MIP_LINEAR," \
                                    "visibility=SHADER_VISIBILITY_PIXEL, comparisonFunc = COMPARISON_LESS_EQUAL )"

#define DepthMap_RS               "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                  "CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
                                  "CBV(b3, visibility=SHADER_VISIBILITY_VERTEX)"

#define DepthMapTransparent_RS    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                  "CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
                                  "CBV(b3, visibility=SHADER_VISIBILITY_VERTEX), " \
                                  "DescriptorTable(SRV(t0, numDescriptors = 1, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                                  "StaticSampler(s0, " \
                                     "addressU = TEXTURE_ADDRESS_WRAP, " \
                                     "addressV = TEXTURE_ADDRESS_WRAP, " \
                                     "addressW = TEXTURE_ADDRESS_WRAP, " \
                                     "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )"

#define Volumetric_RS         "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                              "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "CBV(b3, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "CBV(b5, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "DescriptorTable(SRV(t0, numDescriptors = 2, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                              "StaticSampler(s2, " \
                                    "addressU = TEXTURE_ADDRESS_CLAMP, " \
                                    "addressV = TEXTURE_ADDRESS_CLAMP, " \
                                    "addressW = TEXTURE_ADDRESS_CLAMP, " \
                                    "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )," \
                              "StaticSampler(s1, " \
                                    "addressU = TEXTURE_ADDRESS_BORDER, " \
                                    "addressV = TEXTURE_ADDRESS_BORDER, " \
                                    "addressW = TEXTURE_ADDRESS_BORDER, " \
                                    "filter = FILTER_COMPARISON_MIN_MAG_MIP_LINEAR," \
                                    "visibility=SHADER_VISIBILITY_PIXEL, comparisonFunc = COMPARISON_LESS_EQUAL )"



#define Forward_RS            "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                              "CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
                              "CBV(b1, visibility=SHADER_VISIBILITY_VERTEX), " \
                              "CBV(b4, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "DescriptorTable(SRV(t0, numDescriptors = 1, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                              "StaticSampler(s0, " \
                                     "addressU = TEXTURE_ADDRESS_WRAP, " \
                                     "addressV = TEXTURE_ADDRESS_WRAP, " \
                                     "addressW = TEXTURE_ADDRESS_WRAP, " \
                                     "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )"

#define Ssr_RS                "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                              "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "CBV(b5, visibility=SHADER_VISIBILITY_PIXEL), " \
                              "DescriptorTable(SRV(t0, numDescriptors = 3, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                              "StaticSampler(s0, " \
                                    "addressU = TEXTURE_ADDRESS_CLAMP, " \
                                    "addressV = TEXTURE_ADDRESS_CLAMP, " \
                                    "addressW = TEXTURE_ADDRESS_CLAMP, " \
                                    "filter = FILTER_MIN_MAG_MIP_POINT, visibility=SHADER_VISIBILITY_PIXEL )," \
                              "StaticSampler(s1, " \
                                    "addressU = TEXTURE_ADDRESS_CLAMP, " \
                                    "addressV = TEXTURE_ADDRESS_CLAMP, " \
                                    "addressW = TEXTURE_ADDRESS_CLAMP, " \
                                    "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )," \
                              "StaticSampler(s2, " \
                                    "addressU = TEXTURE_ADDRESS_BORDER, " \
                                    "addressV = TEXTURE_ADDRESS_BORDER, " \
                                    "addressW = TEXTURE_ADDRESS_BORDER, " \
                                    "filter = FILTER_COMPARISON_MIN_MAG_MIP_LINEAR," \
                                    "visibility=SHADER_VISIBILITY_PIXEL)"

#define GodRays_RS               "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                 "CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
                                 "DescriptorTable(SRV(t0, numDescriptors = 1, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
                                 "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_CLAMP, " \
                                 "addressV = TEXTURE_ADDRESS_CLAMP, " \
                                 "addressW = TEXTURE_ADDRESS_CLAMP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL)"


#define LensFlare_RS             "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                 "CBV(b2, visibility=SHADER_VISIBILITY_GEOMETRY), " \
                                 "DescriptorTable(SRV(t0, numDescriptors = 8, flags = DATA_VOLATILE), visibility = SHADER_VISIBILITY_ALL), " \
                                 "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_CLAMP, " \
                                 "addressV = TEXTURE_ADDRESS_CLAMP, " \
                                 "addressW = TEXTURE_ADDRESS_CLAMP, " \
                                 "filter = FILTER_MIN_MAG_MIP_POINT, visibility= SHADER_VISIBILITY_ALL)"

#define DepthOfField_RS         "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                 "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                                 "CBV(b5, visibility=SHADER_VISIBILITY_PIXEL), " \
                                 "DescriptorTable(SRV(t0, numDescriptors = 3, flags = DATA_VOLATILE), visibility = SHADER_VISIBILITY_PIXEL), " \
                                 "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_CLAMP, " \
                                 "addressV = TEXTURE_ADDRESS_CLAMP, " \
                                 "addressW = TEXTURE_ADDRESS_CLAMP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"


#define Add_RS                "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                              "DescriptorTable(SRV(t0, numDescriptors = 2, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
                               "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )"

#define Fog_RS                  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                                "CBV(b5, visibility=SHADER_VISIBILITY_PIXEL), " \
                                "DescriptorTable(SRV(t0, numDescriptors = 2, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL), " \
                                "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )"

#define Clouds_RS               "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                 "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                                 "CBV(b7, visibility=SHADER_VISIBILITY_PIXEL), " \
                                 "DescriptorTable(SRV(t0, numDescriptors = 4, flags = DATA_VOLATILE), visibility = SHADER_VISIBILITY_PIXEL), " \
                                 "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)," \
                                 "StaticSampler(s1, " \
                                 "addressU = TEXTURE_ADDRESS_WRAP, " \
                                 "addressV = TEXTURE_ADDRESS_WRAP, " \
                                 "addressW = TEXTURE_ADDRESS_WRAP, " \
                                 "filter = FILTER_MIN_MAG_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL)"

#define MotionBlur_RS            "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                 "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                                 "CBV(b5, visibility=SHADER_VISIBILITY_PIXEL), " \
                                 "DescriptorTable(SRV(t0, numDescriptors = 2, flags = DATA_VOLATILE), visibility = SHADER_VISIBILITY_PIXEL), " \
                                 "StaticSampler(s0, " \
                                 "addressU = TEXTURE_ADDRESS_CLAMP, " \
                                 "addressV = TEXTURE_ADDRESS_CLAMP, " \
                                 "addressW = TEXTURE_ADDRESS_CLAMP, " \
                                 "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL)"


                
#define TiledLighting_RS         "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                                 "CBV(b6, visibility=SHADER_VISIBILITY_ALL), " \
                                 "DescriptorTable(SRV(t0, numDescriptors = 3, flags = DATA_VOLATILE), visibility = SHADER_VISIBILITY_ALL), " \
                                 "DescriptorTable(UAV(u0, numDescriptors = 2, flags = DATA_VOLATILE), visibility = SHADER_VISIBILITY_ALL), " \
                                 "DescriptorTable(SRV(t3, numDescriptors = 1, flags = DATA_VOLATILE), visibility = SHADER_VISIBILITY_ALL), "

#define ClusterBuilding_RS       "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
                                 "DescriptorTable(UAV(u0, numDescriptors = 1, flags = DATA_VOLATILE), visibility = SHADER_VISIBILITY_ALL)"
    
#define ClusterCulling_RS          "DescriptorTable(SRV(t0, numDescriptors = 2, flags = DATA_VOLATILE, offset=DESCRIPTOR_RANGE_OFFSET_APPEND), visibility = SHADER_VISIBILITY_ALL), " \
                                   "DescriptorTable(UAV(u0, numDescriptors = 3, flags = DATA_VOLATILE, offset=DESCRIPTOR_RANGE_OFFSET_APPEND), visibility = SHADER_VISIBILITY_ALL)"

#define ClusterLightingPBR_RS       "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
                                    "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
                                    "DescriptorTable(SRV(t0, numDescriptors = 3, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                                    "DescriptorTable(SRV(t3, numDescriptors = 3, flags = DATA_VOLATILE), visibility=SHADER_VISIBILITY_PIXEL ), " \
                                    "StaticSampler(s0, " \
                                    "addressU = TEXTURE_ADDRESS_WRAP, " \
                                    "addressV = TEXTURE_ADDRESS_WRAP, " \
                                    "addressW = TEXTURE_ADDRESS_WRAP, " \
                                    "filter = FILTER_MIN_MAG_MIP_LINEAR, visibility=SHADER_VISIBILITY_PIXEL )" 