#ifndef RAKE_SETTINGS_HEADING_H
#define RAKE_SETTINGS_HEADING_H

typedef struct { float r,g,b,a; } R5K_HeadingColor;

static inline void r5k_settings_heading(const DK2ML_API *api, void (*plain)(const char *,...), const char *label, bool primary) {
    static void (*colored)(const R5K_HeadingColor *,const char *,...);
    static void (*space)(void);
    static void (*separator)(int,float);
    static bool resolved;
    if(!resolved){
        colored=(void*)api->ResolveSymbol("ImGui::TextColored");
        space=(void*)api->ResolveSymbol("ImGui::Spacing");
        separator=(void*)api->ResolveSymbol("ImGui::SeparatorEx");
        resolved=true;
    }
    const R5K_HeadingColor color=primary ? (R5K_HeadingColor){1.0f,230.0f/255.0f,0.0f,1.0f}
                                        : (R5K_HeadingColor){1.0f,0.79f,0.29f,1.0f};
    if(space)space();
    if(primary&&separator)separator(1,1.0f); /* ImGuiSeparatorFlags_Horizontal */
    if(colored)colored(&color,"%s",label);
    else plain("%s",label);
}
#endif
