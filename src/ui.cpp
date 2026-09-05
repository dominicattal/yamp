#include "ui.h"
#include "mp.h"
#include <cstring>
#include <imgui.h>
#include <unordered_map>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_freetype.h>
#include <portable-file-dialogs.h>
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define SHOW_RIGHT_NONE 0
#define SHOW_RIGHT_ALBUM 1
#define SHOW_RIGHT_SONG 2
#define SHOW_RIGHT_PLAYLIST 3

#define SHOW_CENTER_NONE 0
#define SHOW_CENTER_ALL_SONGS 1
#define SHOW_CENTER_ALBUM 2
#define SHOW_CENTER_PLAYLIST 3
#define SHOW_CENTER_ARTIST 4
#define SHOW_CENTER_SEARCH_RESULT 5

struct UIContext {
    std::unordered_map<int, GLuint> song_textures;

    GLFWwindow* window;

    GLuint default_texture;
    int default_texture_width;
    int default_texture_height;

    GLuint play_texture;
    int play_texture_width;
    int play_texture_height;

    GLuint queue_texture;
    int queue_texture_width;
    int queue_texture_height;

    bool show_demo_window;

    int right_side;
    int right_side_song_id;

    int center;
    int open_album_id;
    int open_playlist_id;
    int open_artist_id;

    std::vector<int> search_result_songs;
};

static UIContext ctx;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static bool ui_key_callback(int key, int scancode, int action, int mods)
{

    (void)key; (void)scancode; (void)action; (void)mods;
    return ImGui::GetIO().WantCaptureKeyboard;
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)window;
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
        ctx.show_demo_window = !ctx.show_demo_window;
    if (ui_key_callback(key, scancode, action, mods))
        return;
}

static void initialize_default_texture(GLuint* id, const char* path, int* width, int* height)
{
    glGenTextures(1, id);
    glBindTexture(GL_TEXTURE_2D, *id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int nc;
    FILE* fptr = fopen(path, "r");
    unsigned char* data = stbi_load_from_file(fptr, width, height, &nc, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *width, *height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    fclose(fptr);
}

static void initialize_default_textures()
{
    initialize_default_texture(&ctx.default_texture, "assets/No-album-art.png", &ctx.default_texture_width, &ctx.default_texture_height);
    initialize_default_texture(&ctx.play_texture, "assets/play.png", &ctx.play_texture_width, &ctx.play_texture_height);
    initialize_default_texture(&ctx.queue_texture, "assets/add-to-playlist.png", &ctx.queue_texture_width, &ctx.queue_texture_height);
}

static void cleanup_textures()
{
    glDeleteTextures(1, &ctx.default_texture);
    for (auto song_texture : ctx.song_textures)
        glDeleteTextures(1, &song_texture.second);
}

static void song_callback(Song* song)
{
    FrontCover front_cover = mp_song_front_cover_load(song->id);
    if (front_cover.data == nullptr) {
        ctx.song_textures[song->id] = ctx.default_texture;
        return;
    }

    glGenTextures(1, &ctx.song_textures[song->id]);
    glBindTexture(GL_TEXTURE_2D, ctx.song_textures[song->id]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, front_cover.width, front_cover.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, front_cover.data);

    mp_song_front_cover_free(&front_cover);
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
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);
    glfwSetKeyCallback(ctx.window, key_callback);

    initialize_default_textures();
    
    assert(pfd::settings::available() && "Portable File Dialogs are not available on this platform.\n");
    //pfd::settings::verbose(true);
    IMGUI_CHECKVERSION();

    //static const ImWchar icons_ranges[] = { 0xf000, 0xf3ff, 0 }; // Will not be copied by AddFont* so keep in scope.
    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(ctx.window, true);

    const char* glsl_version = nullptr;
    ImGui_ImplOpenGL3_Init(glsl_version);

    mp_ctx.song_callback = song_callback;
}

void ui_cleanup()
{
    cleanup_textures();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(ctx.window);
    glfwTerminate();

    SPDLOG_INFO("UI cleaned up");
}

[[maybe_unused]] static void draw_left_side()
{
    GLuint texture = (mp_ctx.current_song) ? ctx.song_textures[mp_ctx.current_song->id] : ctx.default_texture;
    ImGui::ImageWithBg(texture, ImVec2(200, 200), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    //ImGui::PopStyleVar();
    if (ImGui::Button("Add Song", ImVec2(100, 30))) 
    {
        const std::string title {"Choose files to read"};
        const std::string default_path = pfd::path::home();
        const std::vector<std::string> filters {"All Files", "*"};
        const pfd::opt options = pfd::opt::multiselect;
        std::vector<std::string> song_paths = pfd::open_file(title, default_path, filters, options).result();
        mp_add_songs(song_paths);
    }
    if (ImGui::Button("Add Folder", ImVec2(100, 30))) 
    {
        const std::string title {"Select Any Directory"};
        const std::string default_path = pfd::path::home();
        const std::string folder_path = pfd::select_folder(title, default_path).result();
        mp_recursive_add_songs(folder_path);
    }
    if (ImGui::Button("Skip", ImVec2(100, 30)) || (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_S)))
    {
        mp_queue_skip();
    }

    bool shuffle_copy = mp_ctx.shuffle;
    if (ImGui::Checkbox("Shuffle", &shuffle_copy))
        mp_toggle_shuffle();

    bool autoplay = mp_ctx.autoplay;
    if (ImGui::Checkbox("Autoplay", &autoplay))
        mp_toggle_autoplay();

    if (ImGui::SliderFloat("Volume", &mp_ctx.volume, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_None))
        mp_update_volume();

    char cursor_str[256];
    int cursor = static_cast<int>(mp_ctx.current_song_cursor);
    int length = static_cast<int>(mp_ctx.current_song_length);
    snprintf(cursor_str, sizeof(cursor_str), "%d:%02d / %d:%02d", cursor / 60, cursor % 60, length / 60, length % 60);
    if (ImGui::SliderFloat("Cursor", &mp_ctx.current_song_cursor, 0.0f, mp_ctx.current_song_length, cursor_str, ImGuiSliderFlags_None))
        mp_update_cursor();

    const char* loop_enum[] = {"none", "group", "track"};
    ImGui::Combo("combo", &mp_ctx.loop_mode, loop_enum, std::size(loop_enum));

    bool key_pressed = !ImGui::GetIO().WantCaptureKeyboard && (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_F9));
    if (ImGui::Button("Pause/Resume") || key_pressed)
        mp_pause_or_resume();

    if (ImGui::Button("Clear Queue"))
        mp_queue_clear();

    if (!mp_ctx.current_song) {
        ImGui::Text("No song playing");
    } else {
        ImGui::Text("Name: %s", mp_ctx.current_song->title.c_str());
        //ImGui::Text("Artist: %s", mp_ctx.current_song.artist.c_str());
        //ImGui::Text("Track: %s", mp_ctx.current_song.track.c_str());
    }

    if (mp_ctx.playing_group) {
        if (mp_ctx.group_is_album) {
            const Album* album = mp_get_album_from_id(mp_ctx.group_id);
            ImGui::Text("Playing: %s", album->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Open")) {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album_id = album->id;
            }
        } else {
            const Playlist* playlist = mp_get_playlist_from_id(mp_ctx.group_id);
            ImGui::Text("Playing: %s", playlist->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Open")) {
                ctx.center = SHOW_CENTER_PLAYLIST;
                ctx.open_album_id = playlist->id;
            }
        }
    }

    if (ImGui::BeginTable("Queue", 1, ImGuiTableFlags_None))
    {
        ImGui::TableSetupColumn("Queue", ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();
        for (const Song* song : mp_ctx.queue)
        {
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
        }
        for (auto [song_id, track] : mp_ctx.group_queue)
        {
            const Song* song = mp_get_song_from_id(song_id);
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
        }
        for (const Song* song : mp_ctx.autoplay_queue)
        {
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Create Playlist"))
    {
        ctx.center = SHOW_CENTER_PLAYLIST;
        ctx.open_playlist_id = mp_create_playlist();
        SPDLOG_INFO("{}", ctx.open_playlist_id);
    }

    if (ImGui::BeginTable("Playlists", 2, ImGuiTableFlags_None))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();
        for (const Playlist& playlist : mp_ctx.playlists)
        {
            ImGui::PushID(playlist.id);
            ImGui::TableNextColumn();
            if (ImGui::Button("Show")) {
                ctx.center = SHOW_CENTER_PLAYLIST;
                ctx.open_playlist_id = playlist.id;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%s", playlist.name.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_all_songs()
{
    ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("All Songs", 5, flags, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_NoSort);
        //ImGui::TableSetupScrollFreeze(0, 1);
        //ImGui::TableHeadersRow();
        for (const Song& song : mp_ctx.songs)
        {
            ImGui::PushID(song.id);
            ImGui::TableNextColumn();
            if (ImGui::ImageButton("ABCDE", ctx.queue_texture, ImVec2(32, 32), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 1.0f, 1.0f)))
                mp_queue_song(song.id);
            ImGui::TableNextColumn();

            ImVec2 button_pos = ImGui::GetCursorScreenPos();
            ImVec2 size = ImVec2(50, 50);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::ImageButton("PlayButton", ctx.song_textures[song.id], size))
                mp_play_song(song.id);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetCursorScreenPos(button_pos);
                ImGui::ImageWithBg(ctx.play_texture, size);
            }
            ImGui::PopStyleColor();

            ImGui::TableNextColumn();

            char length_str[256];
            int length = static_cast<int>(song.length);
            snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
            ImGui::Text("%s", length_str);

            ImGui::TableNextColumn();
            ImGui::Text("%s", song.title.c_str());
            int artist_id = mp_get_artist_id_from_song_id(song.id);
            if (artist_id != -1) {
                const Artist* artist = mp_get_artist_from_id(artist_id);
                char artist_str[256];
                snprintf(artist_str, sizeof(artist_str), "%s", artist->name.c_str());
                if (ImGui::Button(artist_str))
                {
                    ctx.center = SHOW_CENTER_ARTIST;
                    ctx.open_artist_id = artist_id;
                }
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("Open Album")) 
            {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album_id = mp_get_album_id_from_song_id(song.id);
            }
            if (ImGui::Button("Add To Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::Button("Open Right Side"))
            {
                ctx.right_side = SHOW_RIGHT_SONG;
                ctx.right_side_song_id = song.id;
            }
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                for (const Playlist& playlist : mp_ctx.playlists)
                {
                    ImGui::PushID(playlist.id);
                    if (ImGui::Button(playlist.name.c_str()))
                        mp_add_song_id_to_playlist_id(song.id, playlist.id);
                    ImGui::PopID();
                }
                if (ImGui::Button("Create Playlist"))
                {
                    ctx.center = SHOW_CENTER_PLAYLIST;
                    ctx.open_playlist_id = mp_create_playlist();
                    mp_add_song_id_to_playlist_id(song.id, ctx.open_playlist_id);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_search_results()
{
    ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("All Songs", 4, flags, ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_NoSort);
        //ImGui::TableSetupScrollFreeze(0, 1);
        //ImGui::TableHeadersRow();
        for (int song_id : ctx.search_result_songs)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(song_id);
            if (ImGui::Button("Play"))
                mp_play_song(song_id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song_id);
            ImGui::TableNextColumn();
            ImGui::ImageWithBg(ctx.song_textures[song_id], ImVec2(50, 50), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImGui::TableNextColumn();
            const Song* song = mp_get_song_from_id(song_id);
            ImGui::Text("%s", song->title.c_str());
            int artist_id = mp_get_artist_id_from_song_id(song_id);
            if (artist_id != -1) {
                const Artist* artist = mp_get_artist_from_id(artist_id);
                assert(artist);
                ImGui::Text("%s", artist->name.c_str());
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("Open Album")) {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album_id = mp_get_album_id_from_song_id(song_id);
            }
            if (ImGui::Button("Add To Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                for (const Playlist& playlist : mp_ctx.playlists)
                {
                    ImGui::PushID(playlist.id);
                    if (ImGui::Button(playlist.name.c_str()))
                        mp_add_song_id_to_playlist_id(song_id, playlist.id);
                    ImGui::PopID();
                }
                if (ImGui::Button("Create Playlist"))
                {
                    ctx.center = SHOW_CENTER_PLAYLIST;
                    ctx.open_playlist_id = mp_create_playlist();
                    mp_add_song_id_to_playlist_id(song_id, ctx.open_playlist_id);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_album_info()
{
    std::vector<SongTrack> tracks = mp_get_song_ids_from_album_id(ctx.open_album_id);
    if (tracks.size() == 0)
        return;

    const Album* album = mp_get_album_from_id(ctx.open_album_id);
    int artist_id = mp_get_artist_id_from_album_id(ctx.open_album_id);
    const Artist* artist = mp_get_artist_from_id(artist_id);

    ImGui::ImageWithBg(ctx.song_textures[tracks[0].song_id], ImVec2(200, 200), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    {
        ImGui::BeginChild("album_view", ImVec2(ImGui::GetContentRegionAvail().x, 200));
        ImGui::SetWindowFontScale(4.0f); 
        ImGui::Text("%s", album->name.c_str());
        ImGui::SetWindowFontScale(2.0f); 
        char artist_buf[256];
        snprintf(artist_buf, sizeof(artist_buf), "%s", artist->name.c_str());
        if (ImGui::Button(artist_buf))
        {
            ctx.center = SHOW_CENTER_ARTIST;
            ctx.open_artist_id = artist_id;
        }
        ImGui::SetWindowFontScale(1.0f); 
        if (ImGui::Button("Queue"))
            for (auto [song_id, track] : tracks)
                mp_queue_song(song_id);
        if (ImGui::Button("Play"))
            mp_play_album(album->id);
        if (ImGui::Button("Add To Playlist"))
            ImGui::OpenPopup("add_to_playlist_popup");
        char length_str[256];
        int length = static_cast<int>(album->length);
        snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
        ImGui::Text("%s", length_str);
        if (ImGui::BeginPopup("add_to_playlist_popup"))
        {
            for (const Playlist& playlist : mp_ctx.playlists)
            {
                ImGui::PushID(playlist.id);
                if (ImGui::Button(playlist.name.c_str()))
                    mp_add_album_id_to_playlist_id(album->id, playlist.id);
                ImGui::PopID();
            }
            if (ImGui::Button("Create Playlist"))
            {
                ctx.center = SHOW_CENTER_PLAYLIST;
                ctx.open_playlist_id = mp_create_playlist();
                mp_add_album_id_to_playlist_id(album->id, ctx.open_playlist_id);
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    }

    if (ImGui::BeginTable("Nested ALbum Songs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 75);
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Song", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        int id = 0;
        for (auto [song_id, track] : tracks)
        {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 40.0f);
            ImGui::TableNextColumn();
            ImGui::PushID(id++);
            if (ImGui::Button("Play"))
                mp_play_song(song_id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song_id);
            ImGui::TableNextColumn();
            ImGui::Text("%d", track);
            ImGui::TableNextColumn();
            const Song* song = mp_get_song_from_id(song_id);
            ImGui::Text("%s", song->title.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_playlist_info()
{
    std::vector<SongTrack> tracks = mp_get_song_ids_from_playlist_id(ctx.open_playlist_id);

    const Playlist* playlist = mp_get_playlist_from_id(ctx.open_playlist_id);

    GLuint texture = (tracks.size() > 0) ? ctx.song_textures[tracks[0].song_id] : ctx.default_texture;

    ImGui::ImageWithBg(texture, ImVec2(200, 200), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    {
        ImGui::BeginChild("playlist_view", ImVec2(ImGui::GetContentRegionAvail().x, 200));
        ImGui::SetWindowFontScale(4.0f); 
        ImGui::Text("%s", playlist->name.c_str());
        ImGui::SetWindowFontScale(1.0f); 
        static char playlist_name[256];
        if (ImGui::Button("Change Name")) {
            std::strncpy(playlist_name, playlist->name.c_str(), sizeof(playlist_name));
            ImGui::OpenPopup("change_playlist_name");
        }

        if (ImGui::Button("Queue"))
            for (auto [song_id, track] : tracks)
                mp_queue_song(song_id);

        if (ImGui::Button("Play"))
            mp_play_playlist(playlist->id);

        char length_str[256];
        int length = static_cast<int>(playlist->length);
        snprintf(length_str, sizeof(length_str), "%d:%02d", length / 60, length % 60);
        ImGui::Text("%s", length_str);

        if (ImGui::BeginPopup("change_playlist_name")) {
            ImGui::Text("Edit name:");
            ImGui::InputText("##edit", playlist_name, IM_COUNTOF(playlist_name));
            if (ImGui::Button("Save") || ImGui::IsKeyPressed(ImGuiKey_Enter))
            {
                mp_rename_playlist_id(playlist->id, playlist_name);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    }

    if (tracks.size() == 0) {
        ImGui::Text("No Songs");
        return;
    }

    if (ImGui::BeginTable("Nested ALbum Songs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 75);
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Song", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        int id = 0;
        for (auto [song_id, track] : tracks)
        {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 40.0f);
            ImGui::TableNextColumn();
            ImGui::PushID(id++);
            if (ImGui::Button("Play"))
                mp_play_song(song_id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song_id);
            ImGui::TableNextColumn();
            ImGui::Text("%d", track);
            ImGui::TableNextColumn();
            const Song* song = mp_get_song_from_id(song_id);
            const Album* album = mp_get_album_from_id(mp_get_album_id_from_song_id(song_id));
            const Artist* artist = mp_get_artist_from_id(mp_get_artist_id_from_song_id(song_id));
            ImGui::Text("%s", song->title.c_str());
            char album_name[256];
            if (album) {
                snprintf(album_name, sizeof(album_name), "%s", album->name.c_str());
                if (ImGui::Button(album_name))
                {
                    ctx.center = SHOW_CENTER_ALBUM;
                    ctx.open_album_id = album->id;
                }
            }
            if (artist) {
                ImGui::Text("%s", artist->name.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_artist_info()
{
    const Artist* artist = mp_get_artist_from_id(ctx.open_artist_id);
    ImGui::SetWindowFontScale(4.0f); 
    ImGui::Text("%s", artist->name.c_str());
    ImGui::SetWindowFontScale(1.0f); 

    std::vector<int> album_ids = mp_get_album_ids_from_artist_id(artist->id);
    std::vector<int> song_ids = mp_get_song_ids_from_artist_id(artist->id);

    ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;
    int width = ImGui::GetContentRegionAvail().x / 2;
    int height = ImGui::GetContentRegionAvail().y;
    if (ImGui::BeginTable("All Songs", 4, flags, ImVec2(width, height)))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_NoSort);
        //ImGui::TableSetupScrollFreeze(0, 1);
        //ImGui::TableHeadersRow();
        for (int song_id : song_ids)
        {
            const Song* song = mp_get_song_from_id(song_id);
            ImGui::TableNextColumn();
            ImGui::PushID(song_id);
            if (ImGui::Button("Play"))
                mp_play_song(song_id);;
            if (ImGui::Button("Queue"))
                mp_queue_song(song_id);
            ImGui::TableNextColumn();
            ImGui::ImageWithBg(ctx.song_textures[song_id], ImVec2(50, 50), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            ImGui::TableNextColumn();
            ImGui::Text("%s", song->title.c_str());
            int artist_id = mp_get_artist_id_from_song_id(song_id);
            if (artist_id != -1) {
                const Artist* artist = mp_get_artist_from_id(artist_id);
                char artist_str[256];
                snprintf(artist_str, sizeof(artist_str), "%s", artist->name.c_str());
                if (ImGui::Button(artist_str))
                {
                    ctx.center = SHOW_CENTER_ARTIST;
                    ctx.open_artist_id = artist_id;
                }
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("Open Album")) {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album_id = mp_get_album_id_from_song_id(song_id);
            }
            if (ImGui::Button("Add To Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                for (const Playlist& playlist : mp_ctx.playlists)
                {
                    ImGui::PushID(playlist.id);
                    if (ImGui::Button(playlist.name.c_str()))
                        mp_add_song_id_to_playlist_id(song_id, playlist.id);
                    ImGui::PopID();
                }
                if (ImGui::Button("Create Playlist"))
                {
                    ctx.center = SHOW_CENTER_PLAYLIST;
                    ctx.open_playlist_id = mp_create_playlist();
                    mp_add_song_id_to_playlist_id(song_id, ctx.open_playlist_id);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SameLine();
    if (ImGui::BeginTable("All Albums", 2, flags, ImVec2(width, height)))
    {
        ImGui::TableSetupColumn("Tmp", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        for (int album_id : album_ids)
        {
            const Album* album = mp_get_album_from_id(album_id);
            ImGui::PushID(album->id);
            ImGui::TableNextColumn();
            ImGui::Text("tmp");
            ImGui::TableNextColumn();
            ImGui::Text("%s", album->name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Open")) 
            {
                ctx.center = SHOW_CENTER_ALBUM;
                ctx.open_album_id = album->id;
            }
            ImGui::SameLine();
            if (ImGui::Button("Queue")) {
                std::vector<SongTrack> song_tracks = mp_get_song_ids_from_album_id(album_id);
                for (auto [song_id, track] : song_tracks)
                    mp_queue_song(song_id);
            }
            ImGui::SameLine();
            if (ImGui::Button("Play"))
                mp_play_album(album->id);
            if (ImGui::Button("Add To Playlist"))
                ImGui::OpenPopup("add_to_playlist_popup");
            if (ImGui::BeginPopup("add_to_playlist_popup"))
            {
                for (const Playlist& playlist : mp_ctx.playlists)
                {
                    ImGui::PushID(playlist.id);
                    if (ImGui::Button(playlist.name.c_str()))
                        mp_add_album_id_to_playlist_id(album->id, playlist.id);
                    ImGui::PopID();
                }
                if (ImGui::Button("Create Playlist"))
                {
                    ctx.center = SHOW_CENTER_PLAYLIST;
                    ctx.open_playlist_id = mp_create_playlist();
                    mp_add_album_id_to_playlist_id(album->id, ctx.open_playlist_id);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void draw_center()
{
    if (ImGui::Button("All"))
        ctx.center = SHOW_CENTER_ALL_SONGS;
    ImGui::SameLine();
    ImGui::Text("Search");
    static char search_query[256];
    ImGui::SameLine();
    if (ImGui::InputTextWithHint("input text (w/ hint)", "Search...", search_query, sizeof(search_query))) 
    {
        ctx.search_result_songs = mp_search_songs(search_query);
        ctx.center = SHOW_CENTER_SEARCH_RESULT;
    }
    if (ImGui::IsItemClicked())
        snprintf(search_query, sizeof(search_query), "");
    if (ImGui::IsKeyPressed(ImGuiKey_F2))
        ctx.center = SHOW_CENTER_ALL_SONGS;

    if (ctx.center == SHOW_CENTER_ALL_SONGS)
        draw_all_songs();
    else if (ctx.center == SHOW_CENTER_ALBUM)
        draw_album_info();
    else if (ctx.center == SHOW_CENTER_PLAYLIST)
        draw_playlist_info();
    else if (ctx.center == SHOW_CENTER_ARTIST)
        draw_artist_info();
    else if (ctx.center == SHOW_CENTER_SEARCH_RESULT)
        draw_search_results();
}

void draw_right_side()
{
    if (ImGui::Button("Close"))
    {
        ctx.right_side = SHOW_RIGHT_NONE;
        return;
    }
    const Song* song = mp_get_song_from_id(ctx.right_side_song_id);
    const int artist_id = mp_get_artist_id_from_song_id(song->id);
    const Artist* artist = mp_get_artist_from_id(artist_id);
    const int album_id = mp_get_album_id_from_song_id(song->id);
    const Album* album = mp_get_album_from_id(album_id);
    ImGui::ImageWithBg(ctx.song_textures[song->id], ImVec2(300, 300));
    ImGui::Text("Title: %s", song->title.c_str());
    ImGui::Text("Artist: %s", artist->name.c_str());
    ImGui::Text("Album: %s", album->name.c_str());
    ImGui::Text("Comment: TBD");
    ImGui::Text("Date: TBD");
    ImGui::Text("Track Number: TBD");
    ImGui::Text("Genre: TBD");
    ImGui::Text("Album Artist: TBD");
    ImGui::Text("ISRC: TBD");
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

    bool right_side_open = ctx.right_side != SHOW_RIGHT_NONE;
    if (ImGui::BeginTable("view", 2 + right_side_open, ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 300);
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_NoSort);

        ImGui::TableNextColumn();
        draw_left_side();
        ImGui::TableNextColumn();
        draw_center();
        if (right_side_open)
        {
            ImGui::TableNextColumn();
            draw_right_side();
        }
        ImGui::EndTable();
    }

    //draw_left_side();
    //ImGui::SameLine();
    //draw_center();
    //ImGui::SameLine();
    //draw_right_side();

    ImGui::End();

    // Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ui_loop()
{
    while (!glfwWindowShouldClose(ctx.window))
    {
        mp_update();
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

