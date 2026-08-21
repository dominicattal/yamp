#include "ui.h"
#include "mp.h"
#include <cstring>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <portable-file-dialogs.h>
#include <iostream>
#include <GLFW/glfw3.h>
#include <vector>

struct UIContext {
    GLFWwindow* window;
    bool show_demo_window;
};

static UIContext ctx;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

bool ui_key_callback(int key, int scancode, int action, int mods)
{

    (void)key; (void)scancode; (void)action; (void)mods;
    return ImGui::GetIO().WantCaptureKeyboard;
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)window;
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
        ctx.show_demo_window = !ctx.show_demo_window;
    if (ui_key_callback(key, scancode, action, mods))
        return;
}

void ui_init()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        exit(1);

    // Select GL version + let the backend select a GLSL version
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + generally GLSL 150
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + generally GLSL 130
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    int window_width = (int)(1280 * main_scale);
    int window_height = (int)(800 * main_scale);

    ctx.window = glfwCreateWindow(window_width, window_height, "yamp", nullptr, nullptr);
    if (ctx.window == nullptr)
        exit(1);
    glfwMakeContextCurrent(ctx.window);
    glfwSwapInterval(1);

    glfwSetKeyCallback(ctx.window, key_callback);
    
    assert(pfd::settings::available() && "Portable File Dialogs are not available on this platform.\n");
    //pfd::settings::verbose(true);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplGlfw_InitForOpenGL(ctx.window, true);

    const char* glsl_version = nullptr;
    ImGui_ImplOpenGL3_Init(glsl_version);
}

void ui_cleanup()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(ctx.window);
    glfwTerminate();
}

static void draw_imgui()
{
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ctx.show_demo_window)
        ImGui::ShowDemoWindow(&ctx.show_demo_window);

    static bool window_open = true;
    ImGuiWindowFlags window_flags{};
    window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("UMP", &window_open, window_flags);

    {
        ImGui::BeginChild("ChildL", ImVec2(200, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_ResizeX, ImGuiWindowFlags_None);
        if (ImGui::Button("Add Song", ImVec2(100, 30))) 
        {
            const std::string title {"Choose files to read"};
            const std::string default_path = pfd::path::home();
            const std::vector<std::string> filters {"All Files", "*"};
            const pfd::opt options = pfd::opt::multiselect;
            std::vector<std::string> song_paths = pfd::open_file(title, default_path, filters, options).result();
            mp_add_songs(song_paths);
        }
        if (ImGui::Button("Skip", ImVec2(100, 30))) 
        {
            mp_queue_skip();
        }

        if (mp_ctx.current_song.title.length() > 0) {
            ImGui::Text("No song playing");
        } else {
            ImGui::Text("Name: %s", mp_ctx.current_song.title.c_str());
            ImGui::Text("Artist: %s", mp_ctx.current_song.artist.c_str());
            ImGui::Text("Track: %s", mp_ctx.current_song.track.c_str());
        }
        if (ImGui::BeginTable("Queue", 1, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("Queue", ImGuiTableColumnFlags_NoSort);
            ImGui::TableHeadersRow();
            for (const std::string& path : mp_ctx.queue)
            {
                ImGui::TableNextColumn();
                ImGui::Text("%s", path.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    ImGui::SameLine();

    {
        ImGui::BeginChild("Main", ImVec2(500, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_ResizeX, ImGuiWindowFlags_None);
        static ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
        if (ImGui::BeginTable("All Songs", 4, flags))
        {
            ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_NoSort);
            ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            int id = 0;
            for (const Song& song : mp_ctx.songs)
            {
                //ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(id++);
                if (ImGui::Button("Play"))
                    mp_play_song(song.path);;
                ImGui::SameLine();
                if (ImGui::Button("Queue"))
                    mp_queue_song(song.path);
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::Text("%s", song.title.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", song.artist.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", song.track.c_str());
            }
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("All Albums", 1, flags))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const Album& album : mp_ctx.albums)
            {
                ImGui::TableNextColumn();
                ImGui::Text("%s", album.name.c_str());
                if (ImGui::BeginTable("Nested ALbum Songs", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable))
                {
                    ImGui::TableSetupColumn("Song");
                    ImGui::TableSetupColumn("Track");
                    ImGui::TableHeadersRow();
                    for (const Song* song : album.songs)
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", song->title.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", song->track.c_str());
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    ImGui::SameLine();

    {
        ImGui::BeginChild("PPP", ImVec2(300, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_ResizeX, ImGuiWindowFlags_None);
        if (ImGui::BeginTable("Playlists", 1, 0) )
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const Playlist& playlist : mp_ctx.playlists)
            {
                ImGui::TableNextColumn();
                ImGui::Text("%s", playlist.name.c_str());
                if (ImGui::BeginTable("Nested ALbum Songs", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable))
                {
                    ImGui::TableSetupColumn("Song");
                    ImGui::TableSetupColumn("Track");
                    ImGui::TableHeadersRow();
                    for (const Song* song : playlist.songs)
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", song->title.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", 0);
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

    }

    ImGui::End();

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ui_loop()
{
    while (!glfwWindowShouldClose(ctx.window))
    {
        glfwPollEvents();
        if (glfwGetWindowAttrib(ctx.window, GLFW_ICONIFIED) != 0)
            continue;

        int display_w, display_h;
        glfwGetFramebufferSize(ctx.window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        draw_imgui();

        glfwSwapBuffers(ctx.window);
    }
}

