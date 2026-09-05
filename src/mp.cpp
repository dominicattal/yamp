#include "mp.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <format>
#include <codecvt>
#include <cassert>
#include <miniaudio.h>
#include <random>
#include <id3v2lib.h>
#include <sqlite3.h>
#include <utf8.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <stb_image.h>

struct MPContextInternal {
    std::mt19937 mt;

    sqlite3* db;
    ma_engine engine;
    ma_sound current_song_sound;
    bool paused;
    bool current_song_loaded;
    bool song_ended;
};

MPContext mp_ctx;
static MPContextInternal ctx;

static void execute_file(const char* path)
{
    auto size = std::filesystem::file_size(path);
    std::string content(size, '\0');
    std::ifstream in(path);
    in.read(&content[0], size);

    char* error_msg{};
    sqlite3_exec(ctx.db, content.c_str(), NULL, NULL, &error_msg);
    if (error_msg != NULL) {
        SPDLOG_INFO("SQLite3 error: %s", error_msg);
        sqlite3_free(error_msg);
    }
}

static int song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int id = std::atoi(values[0]);
    std::string title = values[1];
    std::string path = values[2];
    float length = std::atof(values[3]);
    Song& song = mp_ctx.songs.emplace_back(title, path, length, id);
    //Song& song = mp_ctx.songs.emplace(std::make_pair(id, Song{title, path, id}));
    if (mp_ctx.song_callback)
        mp_ctx.song_callback(&song);
    return 0;
}

static int album_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int album_id = std::atoi(values[0]);
    mp_ctx.albums.emplace_back(values[1], album_id);
    return 0;
}

static int artist_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int artist_id = std::atoi(values[0]);
    mp_ctx.artists.emplace_back(values[1], artist_id);
    return 0;
}

static int playlist_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int playlist_id = std::atoi(values[0]);
    mp_ctx.playlists.emplace_back(values[1], playlist_id);
    return 0;
}

static int album_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int album_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    int track = std::atoi(values[2]);
    mp_ctx.album_songs.emplace_back(album_id, song_id, track);
    return 0;
}

static int artist_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int artist_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    mp_ctx.artist_songs.emplace_back(artist_id, song_id);
    return 0;
}

static int artist_album_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int artist_id = std::atoi(values[0]);
    int album_id = std::atoi(values[1]);
    mp_ctx.artist_albums.emplace_back(artist_id, album_id);
    return 0;
}

static int playlist_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int playlist_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    int track = std::atoi(values[2]);
    mp_ctx.playlist_songs.emplace_back(playlist_id, song_id, track);
    return 0;
}

static void db_init()
{
    ctx.mt.seed(std::chrono::steady_clock::now().time_since_epoch().count());
    sqlite3_open("build/yamp.db", &ctx.db);
    execute_file("assets/sql/schema.sql");

    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(*) FROM Songs";
    sqlite3_prepare(ctx.db, query, -1, &stmt, NULL);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return;
    }
    int num_rows = sqlite3_column_int(stmt, 0);
    SPDLOG_INFO("num rows: {}", num_rows);
    // load the example data
    if (num_rows == 0) {
        //execute_file("assets/sql/insert.sql");
    }
    sqlite3_finalize(stmt);
}

void mp_init()
{
    mp_ctx.volume = 1.0f;
    mp_ctx.shuffle = true;
    db_init();
    ma_result res;
    res = ma_engine_init(NULL, &ctx.engine);
    if (res != MA_SUCCESS)
        exit(1);

    sqlite3_exec(ctx.db, "SELECT * FROM Songs", song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Albums", album_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Artists", artist_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Playlists", playlist_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM AlbumSong", album_song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM ArtistAlbum", artist_album_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM ArtistSong", artist_song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM PlaylistSong", playlist_song_callback, NULL, NULL);
}

void mp_cleanup()
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    ma_engine_uninit(&ctx.engine);
    sqlite3_close(ctx.db);
    SPDLOG_INFO("MP cleaned up");
}

void mp_update()
{
    if (mp_ctx.current_song != nullptr) {
        ma_sound_get_cursor_in_seconds(&ctx.current_song_sound, &mp_ctx.current_song_cursor);
    }
    if (ctx.song_ended) {
        mp_queue_skip();
        ctx.song_ended = false;
    }
}

static std::string read_text_frame(ID3v2_TextFrame* frame)
{
    constexpr int ASCII = 0;
    constexpr int UNICODE_UTF16_WITH_BOM = 1;
    constexpr int UNICODE_UTF16_WITHOUT_BOM = 2;
    constexpr int UNICODE_UTF8 = 3;
    const char* text = frame->data->text;
    switch (frame->data->encoding) {
        case ASCII:
        case UNICODE_UTF8:
            return text;
        case UNICODE_UTF16_WITH_BOM: {
            unsigned int bom = (static_cast<unsigned char>(text[1])<<8) | static_cast<unsigned char>(text[0]);
            assert(bom == 0xFEFF);
            std::u16string str(reinterpret_cast<const char16_t*>(text + 2), (frame->data->size - 4) / 2);
            return utf8::utf16to8(str);
        }
        case UNICODE_UTF16_WITHOUT_BOM: {
            std::u16string str(reinterpret_cast<const char16_t*>(text), (frame->data->size - 2) / 2);
            return utf8::utf16to8(str);
        }
    }
    SPDLOG_INFO("Unrecognized encoding {}", frame->data->encoding);
    return "";
}

void mp_add_song(const std::string& song_path)
{
    ID3v2_TextFrame* frame;
    std::string title{};
    std::string artist_name{};
    std::string album_name{};
    std::string path{song_path};
    int track{};
    ID3v2_Tag* tag = ID3v2_read_tag(song_path.c_str());
    if (!tag) {
        printf("Could not read tag for %s\n", song_path.c_str());
        return;
    }

    frame = ID3v2_Tag_get_title_frame(tag);
    if (frame) {
        title = read_text_frame(frame);
    } else {
        std::filesystem::path path{song_path};
        title = path.stem().string();
    }

    frame = ID3v2_Tag_get_artist_frame(tag);
    if (frame)
        artist_name = read_text_frame(frame);

    frame = ID3v2_Tag_get_track_frame(tag);
    if (frame)
        track = std::stoi(read_text_frame(frame));

    frame = ID3v2_Tag_get_album_frame(tag);
    if (frame)
        album_name = read_text_frame(frame);

    sqlite3_stmt* stmt;
    const char* query;
    query = "INSERT INTO Songs (title, path, length) VALUES (?1, ?2, ?3);";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);

    ma_sound sound;
    float song_length;
    ma_sound_init_from_file(&ctx.engine, path.c_str(), 0, NULL, NULL, &sound);
    ma_sound_get_length_in_seconds(&sound, &song_length);
    ma_sound_uninit(&sound);
    sqlite3_bind_double(stmt, 3, song_length);

    int res = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (res == SQLITE_CONSTRAINT) {
        SPDLOG_WARN("song path {} exists in db already", song_path);
        return;
    }

    query = "SELECT id FROM Songs WHERE title=?1 ORDER BY id DESC LIMIT 1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int song_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    Song& song = mp_ctx.songs.emplace_back(title, path, song_length, song_id);
    if (mp_ctx.song_callback)
        mp_ctx.song_callback(&song);

    SPDLOG_INFO("Created song {} {}", song_id, title);

    int album_id{}, artist_id{};
    if (album_name.size() > 0) {
        query = "SELECT id FROM Albums WHERE name=?1";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, album_name.c_str(), -1, SQLITE_TRANSIENT);
        int res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            query = "INSERT INTO Albums (name) VALUES (?1)";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, album_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            query = "SELECT id FROM Albums WHERE name=?1";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, album_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            album_id = sqlite3_column_int(stmt, 0);
            mp_ctx.albums.emplace_back(album_name, album_id);
            sqlite3_finalize(stmt);

            SPDLOG_INFO("Created album {} {}", album_id, album_name);
        } else {
            album_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        query = "INSERT INTO AlbumSong (album_id, song_id, track) VALUES (?1, ?2, ?3)";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
        sqlite3_bind_int(stmt, 1, album_id);
        sqlite3_bind_int(stmt, 2, song_id);
        sqlite3_bind_int(stmt, 3, track);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        mp_ctx.album_songs.emplace_back(album_id, song_id, track);
    }

    if (artist_name.size() > 0) {
        query = "SELECT id FROM Artists WHERE name=?1";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, artist_name.c_str(), -1, SQLITE_TRANSIENT);
        int res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            query = "INSERT INTO Artists (name) VALUES (?1)";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, artist_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            query = "SELECT id FROM Artists WHERE name=?1";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, artist_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            artist_id = sqlite3_column_int(stmt, 0);
            mp_ctx.artists.emplace_back(artist_name, artist_id);
            sqlite3_finalize(stmt);

            SPDLOG_INFO("Created artist {} {}", artist_id, artist_name);
        } else {
            artist_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        query = "INSERT INTO ArtistSong (artist_id, song_id) VALUES (?1, ?2)";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
        sqlite3_bind_int(stmt, 1, artist_id);
        sqlite3_bind_int(stmt, 2, song_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        mp_ctx.artist_songs.emplace_back(artist_id, song_id);
    }

    if (artist_name.size() > 0 && album_name.size() > 0) {
        assert(artist_id > 0);
        assert(album_id > 0);
        query = "SELECT * FROM ArtistAlbum WHERE artist_id=?1 AND album_id=?2";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
        sqlite3_bind_int(stmt, 1, artist_id);
        sqlite3_bind_int(stmt, 2, album_id);
        int res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            query = "INSERT INTO ArtistAlbum (artist_id, album_id) VALUES (?1, ?2)";
            sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
            sqlite3_bind_int(stmt, 1, artist_id);
            sqlite3_bind_int(stmt, 2, album_id);
            sqlite3_step(stmt);
            SPDLOG_INFO("Associated artist={} album={}", artist_id, album_id);
            mp_ctx.artist_albums.emplace_back(artist_id, album_id);
        }
        sqlite3_finalize(stmt);
    }

    ID3v2_Tag_free(tag);
}

void mp_add_songs(const std::vector<std::string>& song_paths)
{
    for (const std::string& song_path : song_paths)
        mp_add_song(song_path);
}

void mp_recursive_add_songs(const std::string& folder_path)
{
    namespace fs = std::filesystem;

    if (!fs::exists(std::filesystem::path{folder_path}))
        return;

    fs::directory_options options = fs::directory_options::follow_directory_symlink;
    fs::recursive_directory_iterator entries = fs::recursive_directory_iterator(folder_path, options);
    for (auto const& dir_entry : entries)
        mp_add_song(dir_entry.path().string());
}

int mp_create_playlist()
{
    const char* playlist_name = "Unnamed Playlist";

    sqlite3_stmt* stmt;
    const char* query = "INSERT INTO Playlists (name) VALUES (?1)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, playlist_name, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Playlists WHERE name=?1 ORDER BY id DESC";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, playlist_name, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int playlist_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    mp_ctx.playlists.emplace_back(playlist_name, playlist_id);

    return playlist_id;
}

void mp_rename_playlist_id(int playlist_id, const char* new_playlist_name)
{
    sqlite3_stmt* stmt;
    const char* query = "UPDATE Playlists SET name=?1 WHERE id=?2";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, new_playlist_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, playlist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    Playlist* playlist = const_cast<Playlist*>(mp_get_playlist_from_id(playlist_id));
    playlist->name = new_playlist_name;
}

void mp_add_song_id_to_playlist_id(int song_id, int playlist_id)
{
    sqlite3_stmt* stmt;
    int playlist_length = mp_get_num_tracks_in_playlist_id(playlist_id);
    const char* query = "INSERT INTO PlaylistSong (playlist_id, song_id, track) VALUES (?1, ?2, ?3)";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_int(stmt, 1, playlist_id);
    sqlite3_bind_int(stmt, 2, song_id);
    sqlite3_bind_int(stmt, 3, playlist_length+1);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    mp_ctx.playlist_songs.emplace_back(playlist_id, song_id, playlist_length+1);
}

void mp_add_album_id_to_playlist_id(int album_id, int playlist_id)
{
    for (auto [song_id, track] : mp_get_song_ids_from_album_id(album_id))
        mp_add_song_id_to_playlist_id(song_id, playlist_id);
}

static void end_song_callback(void* user_data, ma_sound* sound)
{
    (void)user_data; (void)sound;
    ctx.song_ended = true;
}

void mp_play_song(int song_id)
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    const Song* song = mp_get_song_from_id(song_id);
    ma_sound_config config = ma_sound_config_init();
    config.channelsIn = 1;
    config.pFilePath = song->path.c_str();
    config.endCallback = end_song_callback;
    ma_sound_init_ex(&ctx.engine, &config, &ctx.current_song_sound);
    ma_sound_start(&ctx.current_song_sound);
    ma_sound_get_length_in_seconds(&ctx.current_song_sound, &mp_ctx.current_song_length);
    ctx.current_song_loaded = true;
    mp_ctx.current_song = song;
}

void mp_queue_song(int song_id)
{
    const Song* song = mp_get_song_from_id(song_id);
    mp_ctx.queue.push_back(song);
    if (mp_ctx.queue.size() == 1 && !ctx.current_song_loaded)
        mp_queue_skip();
}

void mp_pause_or_resume()
{
    if (!ctx.current_song_loaded)
        return;
    if (ctx.paused) {
        ctx.paused = false;
        ma_sound_start(&ctx.current_song_sound);
    } else {
        ctx.paused = true;
        ma_sound_stop(&ctx.current_song_sound);
    }
}

void mp_toggle_shuffle()
{
    mp_ctx.shuffle = !mp_ctx.shuffle;
    if (mp_ctx.playing_group) {
        if (mp_ctx.shuffle) {
            std::shuffle(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), ctx.mt);
        } else {
            std::sort(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), [](const SongTrack& pair1, const SongTrack& pair2) {
                    return pair1.track < pair2.track;
                });
        }
    }
}

void mp_toggle_autoplay()
{
    if (mp_ctx.autoplay) {
        mp_ctx.autoplay_queue.clear();
    } else {
        for (int i = 0; i < 10; i++) {
            size_t idx = ctx.mt() % mp_ctx.songs.size();
            const Song* song = mp_get_song_from_id(idx);
            mp_ctx.autoplay_queue.push_back(song);
        }
    }
    mp_ctx.autoplay = !mp_ctx.autoplay;
    if (mp_ctx.autoplay && mp_ctx.current_song == nullptr)
        mp_queue_skip();
}

void mp_update_volume()
{
    ma_sound_set_volume(&ctx.current_song_sound, mp_ctx.volume);
}

void mp_update_cursor()
{
    ma_sound_seek_to_second(&ctx.current_song_sound, mp_ctx.current_song_cursor);
}

static void add_playlist_songs_to_group_queue(int playlist_id)
{
    mp_ctx.group_queue.clear();
    for (const PlaylistSong& playlist_song : mp_ctx.playlist_songs)
        if (playlist_song.playlist_id == playlist_id)
            mp_ctx.group_queue.emplace_back(playlist_song.song_id, playlist_song.track);
    if (mp_ctx.shuffle) {
        std::shuffle(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), ctx.mt);
    } else {
        std::sort(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), [](const SongTrack& pair1, const SongTrack& pair2) {
                return pair1.track < pair2.track;
            });
    }
}

static void add_album_songs_to_group_queue(int album_id)
{
    mp_ctx.group_queue.clear();
    for (const AlbumSong& album_song : mp_ctx.album_songs)
        if (album_song.album_id == album_id)
            mp_ctx.group_queue.emplace_back(album_song.song_id, album_song.track);
    if (mp_ctx.shuffle) {
        std::shuffle(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), ctx.mt);
    } else {
        std::sort(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), [](const SongTrack& pair1, const SongTrack& pair2) {
                return pair1.track < pair2.track;
        });
    }
}

void mp_play_playlist(int playlist_id)
{
    mp_ctx.playing_group = true;
    mp_ctx.group_is_album = false;
    mp_ctx.group_id = playlist_id;
    mp_ctx.queue.clear();
    add_playlist_songs_to_group_queue(playlist_id);
    mp_queue_skip();
}

void mp_play_album(int album_id)
{
    mp_ctx.playing_group = true;
    mp_ctx.group_is_album = true;
    mp_ctx.group_id = album_id;
    mp_ctx.queue.clear();
    add_album_songs_to_group_queue(album_id);
    mp_queue_skip();
}

FrontCover mp_song_front_cover_load(int song_id)
{
    FrontCover front_cover{};
    const Song* song = mp_get_song_from_id(song_id);
    ID3v2_Tag* tag = ID3v2_read_tag(song->path.c_str());
    if (!tag)
        return front_cover;
    ID3v2_ApicFrame* apic_cover = ID3v2_Tag_get_album_cover_frame(tag);
    if (!apic_cover)
        return front_cover;
    unsigned char* apic_data = (unsigned char*)apic_cover->data->data;
    int picture_size = apic_cover->data->picture_size;
    int num_channels;
    unsigned char* raw_data = stbi_load_from_memory(apic_data, picture_size, &front_cover.width, &front_cover.height, &num_channels, 4);
    front_cover.data = raw_data;
    ID3v2_Tag_free(tag);
    return front_cover;
}

std::vector<int> mp_search_songs(const char* search_query)
{
    std::vector<int> result{};
    sqlite3_stmt* stmt;
    constexpr int limit = 50;
    const char* query = "SELECT id FROM Songs WHERE title LIKE ?1 LIMIT ?2";
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%%%s%%", search_query);
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, buffer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    return result;
}

void mp_song_front_cover_free(FrontCover* front_cover)
{
    if (front_cover->data)
        stbi_image_free(front_cover->data);
}

const Song* mp_get_song_from_id(int id)
{
    for (const Song& song : mp_ctx.songs)
        if (song.id == id)
            return &song;
    return nullptr;
}

const Album* mp_get_album_from_id(int id)
{
    for (const Album& album : mp_ctx.albums)
        if (album.id == id)
            return &album;
    return nullptr;
}

const Artist* mp_get_artist_from_id(int id)
{
    for (const Artist& artist : mp_ctx.artists)
        if (artist.id == id)
            return &artist;
    return nullptr;
}

const Playlist* mp_get_playlist_from_id(int id)
{
    for (const Playlist& playlist : mp_ctx.playlists)
        if (playlist.id == id)
            return &playlist;
    return nullptr;
}

int mp_get_artist_id_from_song_id(int song_id)
{
    for (const ArtistSong& artist_song : mp_ctx.artist_songs)
        if (artist_song.song_id == song_id)
            return artist_song.artist_id;
    return -1;
}

int mp_get_artist_id_from_album_id(int album_id)
{
    for (const ArtistAlbum& artist_album : mp_ctx.artist_albums)
        if (artist_album.album_id == album_id)
            return artist_album.artist_id;
    return -1;
}

std::vector<SongTrack> mp_get_song_ids_from_album_id(int album_id)
{
    std::vector<SongTrack> result{};
    for (const AlbumSong& album_song : mp_ctx.album_songs) {
        if (album_song.album_id == album_id)
            result.emplace_back(album_song.song_id, album_song.track);
    }
    std::sort(result.begin(), result.end(), [](const SongTrack& pair1, const SongTrack& pair2) {
                return pair1.track < pair2.track;
            });
    return result;
}

int mp_get_num_tracks_in_playlist_id(int playlist_id)
{
    int count = 0;
    for (const PlaylistSong& playlist_song : mp_ctx.playlist_songs)
        if (playlist_song.playlist_id == playlist_id)
            ++count;
    return count;
}

std::vector<SongTrack> mp_get_song_ids_from_playlist_id(int playlist_id)
{
    std::vector<SongTrack> result{};
    for (const PlaylistSong& playlist_song : mp_ctx.playlist_songs) {
        if (playlist_song.playlist_id == playlist_id)
            result.emplace_back(playlist_song.song_id, playlist_song.track);
    }
    std::sort(result.begin(), result.end(), [](const SongTrack& pair1, const SongTrack& pair2) {
                return pair1.track < pair2.track;
            });
    return result;
}

std::vector<int> mp_get_song_ids_from_artist_id(int artist_id)
{
    std::vector<int> result{};
    for (const ArtistSong& artist_song : mp_ctx.artist_songs)
        if (artist_song.artist_id == artist_id)
            result.push_back(artist_song.song_id);
    return result;
}

std::vector<int> mp_get_album_ids_from_artist_id(int artist_id)
{
    std::vector<int> result{};
    for (const ArtistAlbum& artist_album : mp_ctx.artist_albums)
        if (artist_album.artist_id == artist_id)
            result.push_back(artist_album.album_id);
    return result;
}

int mp_get_album_id_from_song_id(int song_id)
{
    for (const AlbumSong& album_song : mp_ctx.album_songs)
        if (album_song.song_id == song_id)
            return album_song.album_id;
    return -1;
}

int mp_get_album_id_from_artist_id(int artist_id)
{
    for (const ArtistAlbum& artist_album : mp_ctx.artist_albums)
        if (artist_album.artist_id == artist_id)
            return artist_album.album_id;
    return -1;
}

static void play_next_group_song()
{
    if (mp_ctx.group_queue.size() == 0) 
    {
        if (mp_ctx.loop_mode == LOOP_NONE) {
            mp_ctx.playing_group = false;
            return;
        }

        if (mp_ctx.group_is_album)
            add_album_songs_to_group_queue(mp_ctx.group_id);
        else
            add_playlist_songs_to_group_queue(mp_ctx.group_id);

        if (mp_ctx.group_queue.size() == 0) {
            SPDLOG_WARN("Tried to play group with no songs");
            mp_ctx.playing_group = false;
            return;
        }
    }
    SongTrack song_track = mp_ctx.group_queue.front();
    const Song* song = mp_get_song_from_id(song_track.song_id);
    mp_ctx.group_queue.pop_front();
    mp_play_song(song->id);
}

void mp_queue_skip()
{
    if (mp_ctx.loop_mode == LOOP_TRACK) {
        mp_play_song(mp_ctx.current_song->id);
        return;
    }
    ctx.paused = false;
    if (mp_ctx.queue.size() == 0) {
        if (mp_ctx.playing_group) {
            play_next_group_song();
        } else if (mp_ctx.autoplay) {
            const Song* song = mp_ctx.autoplay_queue.front();
            mp_ctx.autoplay_queue.pop_front();
            size_t idx = ctx.mt() % mp_ctx.songs.size();
            mp_ctx.autoplay_queue.push_back(mp_get_song_from_id(idx));
            mp_play_song(song->id);
        } else if (ctx.current_song_loaded) {
            mp_ctx.current_song = nullptr;
            ma_sound_uninit(&ctx.current_song_sound);
            ctx.current_song_loaded = false;
        }
    } else {
        const Song* song = mp_ctx.queue.front();
        mp_ctx.queue.pop_front();
        mp_play_song(song->id);
    }
}

void mp_queue_clear()
{
    mp_ctx.queue.clear();
}
