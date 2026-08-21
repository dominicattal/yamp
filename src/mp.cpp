#include "mp.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <format>
#include <cassert>
#include <miniaudio.h>
#include <id3v2lib.h>
#include <sqlite3.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

struct MPContextInternal {
    ma_engine engine;
    ID3v2_Tag* current_song_tag;
    ma_sound current_song_sound;
    sqlite3* db;
};

MPContext mp_ctx;
static MPContextInternal ctx;

static void execute_file(const char* path)
{
    auto size = std::filesystem::file_size(path);
    std::string content(size, '\0');
    std::ifstream in(path);
    in.read(&content[0], size);

    char* error_msg;
    sqlite3_exec(ctx.db, content.c_str(), NULL, NULL, &error_msg);
    if (error_msg != NULL) {
        SPDLOG_INFO("SQLite3 error: %s", error_msg);
        sqlite3_free(error_msg);
    }
}

static void reset_current_song()
{
    mp_ctx.current_song.title = "";
    mp_ctx.current_song.artist = "";
    mp_ctx.current_song.track = "";
    mp_ctx.current_song.path = "";
    mp_ctx.current_song.id = 0;
}

static int song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int id = std::atoi(values[0]);
    std::string title = values[1];
    std::string artist = values[2];
    //int date = std::atoi(values[3]);
    std::string track = values[4];
    std::string path = values[5];
    mp_ctx.songs.emplace_back(title, artist, track, path, id);
    SPDLOG_INFO("Loaded song: {}|{}|{}", values[1], values[2], values[4]);
    return 0;
}

static int album_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    Album* album = static_cast<Album*>(data);
    int song_id = std::atoi(values[0]);
    album->songs.push_back(mp_get_song_by_id(song_id));
    return 0;
}

static int album_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int album_id = std::atoi(values[0]);
    mp_ctx.albums.emplace_back(values[1], std::vector<Song*>{}, album_id);
    std::string query = std::format("SELECT * FROM AlbumSong WHERE album_id={}", album_id);
    sqlite3_exec(ctx.db, query.c_str(), album_song_callback, &mp_ctx.albums.back(), NULL);
    return 0;
}

static int playlist_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    Playlist* playlist = static_cast<Playlist*>(data);
    int song_id = std::atoi(values[0]);
    playlist->songs.push_back(mp_get_song_by_id(song_id));
    return 0;
}

static int playlist_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int playlist_id = std::atoi(values[0]);
    mp_ctx.playlists.emplace_back(values[1], std::vector<Song*>{}, playlist_id);
    std::string query = std::format("SELECT * FROM PlaylistSong WHERE playlist_id={}", playlist_id);
    sqlite3_exec(ctx.db, query.c_str(), playlist_song_callback, &mp_ctx.playlists.back(), NULL);
    return 0;
}

static void db_init()
{
    sqlite3_open("build/mp.db", &ctx.db);

    execute_file("sql/schema.sql");

    sqlite3_stmt* stmt;
    const char* query = "SELECT COUNT(*) FROM Songs";
    sqlite3_prepare(ctx.db, query, -1, &stmt, NULL);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) {
        puts("failed");
        sqlite3_finalize(stmt);
        return;
    }
    int num_rows = sqlite3_column_int(stmt, 0);
    SPDLOG_INFO("num rows: {}", num_rows);
    // load the example data
    if (num_rows == 0) {
        //execute_file("sql/insert.sql");
    }
    sqlite3_finalize(stmt);
}

void mp_init()
{
    db_init();
    ma_result res;
    res = ma_engine_init(NULL, &ctx.engine);
    if (res != MA_SUCCESS)
        exit(1);

    sqlite3_exec(ctx.db, "SELECT * FROM Songs", song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Albums", album_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Playlists", playlist_callback, NULL, NULL);

    reset_current_song();
}

void mp_cleanup()
{
    if (ctx.current_song_tag != nullptr) {
        ma_sound_uninit(&ctx.current_song_sound);
        ID3v2_Tag_free(ctx.current_song_tag);
    }
    ma_engine_uninit(&ctx.engine);
    sqlite3_close(ctx.db);
}

void mp_add_song(const std::string& song_path)
{
    ID3v2_TextFrame* frame;
    std::string title{""};
    std::string artist{""};
    std::string track{"1"};
    std::string album_name{""};
    std::string path{song_path};
    ID3v2_Tag* tag = ID3v2_read_tag(song_path.c_str());
    if (!tag) {
        puts("Could not read tag");
        return;
    }
    frame = ID3v2_Tag_get_title_frame(tag);
    if (frame) {
        title = frame->data->text;
    } else {
        std::filesystem::path path{song_path};
        title = path.stem().string();
    }
    frame = ID3v2_Tag_get_artist_frame(tag);
    if (frame)
        artist = frame->data->text;
    frame = ID3v2_Tag_get_track_frame(tag);
    if (frame)
        track = frame->data->text;
    frame = ID3v2_Tag_get_album_frame(tag);
    if (frame)
        album_name = frame->data->text;

    SPDLOG_INFO(track);

    sqlite3_stmt* stmt;
    const char* query;
    query = "INSERT INTO Songs (title, artist, date, track, path) VALUES (?1, ?2, 0, ?3, ?4);";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, track.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Songs WHERE title=?1 ORDER BY id DESC LIMIT 1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int song_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    SPDLOG_INFO("song id: {}", song_id);

    Song* song = &mp_ctx.songs.emplace_back(title, artist, track, path, song_id);

    if (album_name.size() > 0) {
        int album_id;
        query = "SELECT id FROM Albums WHERE name=?1";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, album_name.c_str(), -1, SQLITE_TRANSIENT);
        int res = sqlite3_step(stmt);
        if (res != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            SPDLOG_INFO("Creating album {}", album_name);
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
            mp_ctx.albums.emplace_back(album_name, std::vector<Song*>{}, album_id);
            sqlite3_finalize(stmt);
        } else {
            album_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        query = "INSERT INTO AlbumSong (song_id, album_id) VALUES (?1, ?2)";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
        sqlite3_bind_int(stmt, 1, song_id);
        sqlite3_bind_int(stmt, 2, album_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        SPDLOG_INFO("album id: {}", album_id);
        Album* album = mp_get_album_by_id(album_id);
        assert(album != nullptr);
        album->songs.push_back(song);
    }
}

void mp_add_songs(const std::vector<std::string>& song_paths)
{
    for (const std::string& song_path : song_paths)
        mp_add_song(song_path);
}

void mp_play_song(const std::string& song_path)
{
    const char* song_path_c_str = song_path.c_str();

    if (ctx.current_song_tag != nullptr) {
        ma_sound_uninit(&ctx.current_song_sound);
        ID3v2_Tag_free(ctx.current_song_tag);
    }

    ma_sound_init_from_file(&ctx.engine, song_path_c_str, 0, NULL, NULL, &ctx.current_song_sound);
    ma_sound_start(&ctx.current_song_sound);

    ctx.current_song_tag = ID3v2_read_tag(song_path_c_str);

    ID3v2_TextFrame* frame;
    reset_current_song();
    frame = ID3v2_Tag_get_title_frame(ctx.current_song_tag);
    if (frame)
        mp_ctx.current_song.title = frame->data->text;
    frame = ID3v2_Tag_get_album_frame(ctx.current_song_tag);
    frame = ID3v2_Tag_get_artist_frame(ctx.current_song_tag);
    if (frame)
        mp_ctx.current_song.artist = frame->data->text;
    frame = ID3v2_Tag_get_track_frame(ctx.current_song_tag);
    if (frame)
        mp_ctx.current_song.track = frame->data->text;
}

void mp_queue_song(const std::string& song_path)
{
    mp_ctx.queue.push_back(song_path);
}

void mp_queue_songs(const std::vector<std::string>& song_paths)
{
    for (const std::string& song_path : song_paths)
        mp_queue_song(song_path);
}

Song* mp_get_song_by_id(int id)
{
    for (Song& song : mp_ctx.songs)
        if (song.id == id)
            return &song;
    return nullptr;
}

Album* mp_get_album_by_id(int id)
{
    for (Album& album : mp_ctx.albums) {
        SPDLOG_INFO("{} {}", album.id, id);
        if (album.id == id)
            return &album;
    }
    return nullptr;
}

void mp_queue_skip()
{
    if (mp_ctx.queue.size() == 0)
        return;

    std::string song_path = mp_ctx.queue.front();
    mp_ctx.queue.pop_front();
    mp_play_song(song_path);
}
