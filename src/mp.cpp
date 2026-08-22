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
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
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
}

static int song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int id = std::atoi(values[0]);
    std::string title = values[1];
    std::string path = values[2];
    mp_ctx.songs.emplace_back(title, path, id);
    SPDLOG_INFO("Loaded song: {} [{}]", values[0], values[1], values[2]);
    return 0;
}

static int album_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int album_id = std::atoi(values[0]);
    mp_ctx.albums.emplace_back(values[1], album_id);
    SPDLOG_INFO("Loaded album: {} [{}]", values[0], values[1]);
    return 0;
}

static int artist_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int artist_id = std::atoi(values[0]);
    mp_ctx.artists.emplace_back(values[1], artist_id);
    SPDLOG_INFO("Loaded artist: {} [{}]", values[0], values[1]);
    return 0;
}

static int playlist_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)keys;
    int playlist_id = std::atoi(values[0]);
    mp_ctx.playlists.emplace_back(values[1], playlist_id);
    SPDLOG_INFO("Loaded playlist: {} [{}]", values[0], values[1]);
    return 0;
}

static int album_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int album_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    int track = std::atoi(values[2]);
    mp_ctx.album_song.emplace_back(album_id, song_id, track);
    SPDLOG_INFO("Loaded album song: album_id={} song_id={}", values[0], values[1]);
    return 0;
}

static int artist_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int artist_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    mp_ctx.artist_song.emplace_back(artist_id, song_id);
    SPDLOG_INFO("Loaded artist song: artist_id={} song_id={}", values[0], values[1]);
    return 0;
}

static int artist_album_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int artist_id = std::atoi(values[0]);
    int album_id = std::atoi(values[1]);
    mp_ctx.artist_album.emplace_back(artist_id, album_id);
    SPDLOG_INFO("Loaded artist album: artist_id={} album_id={}", values[0], values[1]);
    return 0;
}

static int playlist_song_callback(void* data, int num_cols, char** values, char** keys)
{
    (void)data; (void)num_cols; (void)values; (void)keys;
    int playlist_id = std::atoi(values[0]);
    int song_id = std::atoi(values[1]);
    int track = std::atoi(values[2]);
    mp_ctx.playlist_song.emplace_back(playlist_id, song_id, track);
    SPDLOG_INFO("Loaded playlist song: playlist_id={} song_id={} track={}", values[0], values[1], values[2]);
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
    sqlite3_exec(ctx.db, "SELECT * FROM Artists", artist_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM Playlists", playlist_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM AlbumSong", album_song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM ArtistAlbum", artist_album_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM ArtistSong", artist_song_callback, NULL, NULL);
    sqlite3_exec(ctx.db, "SELECT * FROM PlaylistSong", playlist_song_callback, NULL, NULL);

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
    std::string artist_name{""};
    std::string album_name{""};
    std::string path{song_path};
    int track{};
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
        artist_name = frame->data->text;
    frame = ID3v2_Tag_get_track_frame(tag);
    if (frame)
        track = std::atoi(frame->data->text);
    frame = ID3v2_Tag_get_album_frame(tag);
    if (frame)
        album_name = frame->data->text;

    SPDLOG_INFO(track);

    sqlite3_stmt* stmt;
    const char* query;
    query = "INSERT INTO Songs (title, path) VALUES (?1, ?2);";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    query = "SELECT id FROM Songs WHERE title=?1 ORDER BY id DESC LIMIT 1";
    sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL); 
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int song_id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    SPDLOG_INFO("Created song {} {}", song_id, title);

    mp_ctx.songs.emplace_back(title, path, song_id);

    int album_id, artist_id;
    if (album_name.size() > 0) {
        int album_id;
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
        mp_ctx.album_song.emplace_back(album_id, song_id, track);
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
            mp_ctx.albums.emplace_back(artist_name, artist_id);
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
        mp_ctx.artist_song.emplace_back(artist_id, album_id);
    }

    if (artist_name.size() > 0 && album_name.size() > 0) {
        query = "INSERT INTO ArtistAlbum (artist_id, album_id) VALUES (?1, ?2)";
        sqlite3_prepare_v2(ctx.db, query, -1, &stmt, NULL);
        sqlite3_bind_int(stmt, 1, artist_id);
        sqlite3_bind_int(stmt, 2, album_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
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

Artist* mp_get_artist_by_id(int id)
{
    for (Artist& artist : mp_ctx.artists) {
        SPDLOG_INFO("{} {}", artist.id, id);
        if (artist.id == id)
            return &artist;
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
