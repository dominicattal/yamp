#include "mp.h"
#include "config.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <format>
#include <codecvt>
#include <cassert>
#include <miniaudio.h>
#include <random>
#include <fileref.h>
#include <tag.h>
#include <tstringlist.h>
#include <utf8.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <thread>
#include <condition_variable>

struct MPContextInternal {
    std::mt19937 mt;
    ma_engine engine;
    ma_sound current_song_sound;
    bool current_song_loaded;
    bool song_ended;
};

MPContext mp_ctx;
static MPContextInternal ctx;

static void add_song_from_path(const std::string& song_path)
{
    TagLib::FileRef mp3_file_ref(song_path.c_str());

    if (mp3_file_ref.isNull() || !mp3_file_ref.tag()) {
        SPDLOG_ERROR("Could not read {}", song_path);
        return;
    }

    TagLib::Tag* tag = mp3_file_ref.tag();
    assert(tag);
    TagLib::AudioProperties* properties = mp3_file_ref.audioProperties();
    assert(properties);
    std::string title = tag->title().to8Bit(true);
    std::string album_name = tag->album().to8Bit(true);
    std::string artist_name = tag->artist().to8Bit(true);
    unsigned int year = tag->year();
    double song_length = properties->lengthInMilliseconds() / 1000.0;
    int track = tag->track();

    SongID song_id = db_get_song_id(song_path.c_str());
    if (song_id != INVALID_ID)
    {
        SPDLOG_WARN("Song id {} already exists", song_id);
        return;
    }

    uint8_t* art_data = nullptr;
    int art_size = 0;

    auto set_song_art_data = [&mp3_file_ref, &art_data, &art_size]()
        {
            TagLib::List<TagLib::VariantMap> props = mp3_file_ref.complexProperties("PICTURE");
            if (props.isEmpty())
                return;
            const TagLib::VariantMap& map = props.front();
            if (!map.contains("data"))
                return;
            int input_w, input_h, nc;
            const TagLib::ByteVector data = map["data"].toByteVector();
            uint8_t* raw_art_data = stbi_load_from_memory(reinterpret_cast<const uint8_t*>(data.data()), data.size(), &input_w, &input_h, &nc, 4);
            art_data = stbir_resize_uint8_linear(
                        raw_art_data, input_w, input_h, 0,
                        NULL, LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE, 0,
                        STBIR_4CHANNEL
                    );
            art_size = LARGE_COVER_ART_SIZE * LARGE_COVER_ART_SIZE * 4;
            stbi_image_free(raw_art_data);
        };

    set_song_art_data();
    db_create_song(title.c_str(), song_path.c_str(), year, song_length, art_data, art_size);
    if (art_data != nullptr)
        free(art_data);

    song_id = db_get_song_id(song_path.c_str());
    mp_ctx.added_songs.push_back(song_id);
    SPDLOG_INFO("Created song {} {}", song_id, title);

    AlbumID album_id{};
    ArtistID artist_id{};

    if (album_name.size() > 0) 
    {
        album_id = db_get_album_id(album_name.c_str());
        if (album_id == INVALID_ID) 
        {
            bool loaded_art = false;
            for (const char* cover_path : std::array<const char*, 2>{"cover.jpg", "cover.png"})
            {
                std::filesystem::path fs_song_path = std::filesystem::path(song_path).replace_filename(cover_path);
                if (std::filesystem::exists(fs_song_path))
                {
                    int input_w, input_h, nc;
                    uint8_t* raw_art_data = stbi_load(fs_song_path.c_str(), &input_w, &input_h, &nc, 4);
                    uint8_t* album_art_data = stbir_resize_uint8_linear(
                                raw_art_data, input_w, input_h, 0,
                                NULL, LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE, 0,
                                STBIR_4CHANNEL
                            );
                    int album_art_size = LARGE_COVER_ART_SIZE * LARGE_COVER_ART_SIZE * 4;
                    stbi_image_free(raw_art_data);

                    db_create_album(album_name.c_str(), year, album_art_data, album_art_size);
                    if (album_art_data != nullptr)
                        free(album_art_data);

                    loaded_art = true;

                    SPDLOG_INFO("Loaded album {} art {}", album_id, fs_song_path.c_str());

                    break;
                }
            }

            if (!loaded_art)
            {
                Art art = mp_get_song_art(song_id);
                db_create_album(album_name.c_str(), year, art.data, art.size);
                mp_free_art(&art);

                SPDLOG_INFO("Loaded album {} art from song {}", album_id, song_id);
            }

            album_id = db_get_album_id(album_name.c_str());
            mp_ctx.added_artists.push_back(album_id);
            SPDLOG_INFO("Create album {} {}", album_id, album_name);
        }
        db_create_album_song(album_id, song_id, track);
    }

    if (artist_name.size() > 0) 
    {
        artist_id = db_get_artist_id(artist_name.c_str());
        if (artist_id == INVALID_ID)
        {
            db_create_artist(artist_name.c_str());
            artist_id = db_get_artist_id(artist_name.c_str());
            mp_ctx.added_artists.push_back(artist_id);
            SPDLOG_INFO("Create artist {} {}", artist_id, artist_name);
        }

        db_create_artist_song(artist_id, song_id);
    }

    if (album_id != 0 && artist_id != 0 && !db_exists_artist_album(artist_id, album_id))
        db_create_artist_album(artist_id, album_id);
}

void mp_add_song(std::string&& song_path)
{
    mp_ctx.adding_songs = true;
    add_song_from_path(song_path);
}

void mp_add_songs(std::vector<std::string>&& song_paths)
{
    mp_ctx.adding_songs = true;
    for (std::string& song_path : song_paths)
        add_song_from_path(song_path);
}

void mp_recursive_add_songs(const std::string& folder_path)
{
    namespace fs = std::filesystem;

    if (!fs::exists(std::filesystem::path{folder_path}))
        return;

    mp_ctx.num_songs_to_add = 0;
    mp_ctx.num_songs_added = 0;

    fs::directory_options options = fs::directory_options::follow_directory_symlink;
    fs::recursive_directory_iterator entries = fs::recursive_directory_iterator(folder_path, options);
    for (auto const& dir_entry : entries)
    {
        (void)dir_entry;
        mp_ctx.num_songs_to_add++;
    }

    entries = fs::recursive_directory_iterator(folder_path, options);
    mp_ctx.adding_songs = true;
    for (auto const& dir_entry : entries)
    {
        {
            std::lock_guard lock{mp_ctx.adding_songs_mutex};
            mp_ctx.adding_songs_current_path = dir_entry.path().string();
        }
        add_song_from_path(dir_entry.path().string());
        mp_ctx.num_songs_added++;
    }
}

void mp_reset_loaded_songs()
{
    mp_ctx.adding_songs = false;
    mp_ctx.added_songs.clear();
    mp_ctx.added_artists.clear();
    mp_ctx.added_albums.clear();
}

void mp_rename_playlist(PlaylistID playlist_id, const char* new_playlist_name)
{
    db_update_playlist(playlist_id, new_playlist_name);
    LruCacheRef<Playlist> playlist = mp_get_playlist(playlist_id);
    playlist->name = new_playlist_name;
}

void mp_add_song_to_playlist(SongID song_id, PlaylistID playlist_id)
{
    std::vector<SongTrackID> tracks = db_get_songs_from_playlist(playlist_id);
    int track = tracks.size() + 1;
    db_create_playlist_song(playlist_id, song_id, track);
    tracks.emplace_back(song_id, track);
    LruCacheRef<Song> song = mp_get_song(song_id);
    LruCacheRef<Playlist> playlist = mp_ctx.playlists.get(playlist_id);
    if (playlist != nullptr)
        playlist->length += song->length;
}

void mp_add_album_to_playlist(AlbumID album_id, PlaylistID playlist_id)
{
    std::vector<SongTrackID> song_tracks = mp_get_songs_from_album(album_id);
    for (auto & [song_id, track] : song_tracks) {
        LruCacheRef<Song> song = mp_get_song(song_id);
        mp_add_song_to_playlist(song->id, playlist_id);
    }
}

static void end_song_callback(void* user_data, ma_sound* sound)
{
    (void)user_data; (void)sound;
    ctx.song_ended = true;
}

void mp_play_song(SongID song_id)
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    LruCacheRef<Song> song = mp_get_song(song_id);
    ma_sound_config config = ma_sound_config_init();
    config.channelsIn = 1;
    config.pFilePath = song->path.c_str();
    config.endCallback = end_song_callback;
    ma_sound_init_ex(&ctx.engine, &config, &ctx.current_song_sound);
    ma_sound_start(&ctx.current_song_sound);
    ma_sound_get_length_in_seconds(&ctx.current_song_sound, &mp_ctx.current_song_length);
    ctx.current_song_loaded = true;
    mp_ctx.paused = false;
    mp_ctx.current_song = std::move(song);
    mp_ctx.current_song_artist = mp_get_artist_from_song(song_id);
    mp_ctx.current_song_album = mp_get_album_from_song(song_id);
    if (mp_ctx.current_song != nullptr)
        mp_update_volume(mp_ctx.current_song->volume);

    db_get_song_lyrics(song_id, mp_ctx.current_song_lyrics, sizeof(mp_ctx.current_song_lyrics));
}

void mp_queue_song(SongID song_id)
{
    LruCacheRef<Song> song = mp_get_song(song_id);
    mp_ctx.queue.push_back(std::move(song));
    if (mp_ctx.queue.size() == 1 && !ctx.current_song_loaded)
        mp_play_next();
}

void mp_pause_or_resume()
{
    if (!ctx.current_song_loaded)
        return;
    if (mp_ctx.paused) {
        mp_ctx.paused = false;
        ma_sound_start(&ctx.current_song_sound);
    } else {
        mp_ctx.paused = true;
        ma_sound_stop(&ctx.current_song_sound);
    }
}

void mp_go_forward_in_current_song(float seconds)
{
    if (!ctx.current_song_loaded)
        return;

    const ma_uint32 sample_rate = ctx.current_song_sound.engineNode.resampler.sampleRateIn;
    ma_uint64 current_pcm_frame;
    ma_sound_get_cursor_in_pcm_frames(&ctx.current_song_sound, &current_pcm_frame);

    ma_uint64 skip_pcm_frames = static_cast<ma_uint64>(seconds) * sample_rate;
    current_pcm_frame += skip_pcm_frames;

    ma_sound_seek_to_pcm_frame(&ctx.current_song_sound, current_pcm_frame);
}

void mp_go_backward_in_current_song(float seconds)
{
    if (!ctx.current_song_loaded)
        return;

    const ma_uint32 sample_rate = ctx.current_song_sound.engineNode.resampler.sampleRateIn;
    ma_uint64 current_pcm_frame;
    ma_sound_get_cursor_in_pcm_frames(&ctx.current_song_sound, &current_pcm_frame);

    ma_uint64 skip_pcm_frames = static_cast<ma_uint64>(seconds) * sample_rate;
    if (skip_pcm_frames > current_pcm_frame)
        current_pcm_frame = 0;
    else
        current_pcm_frame -= skip_pcm_frames;

    ma_sound_seek_to_pcm_frame(&ctx.current_song_sound, current_pcm_frame);
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
            LruCacheRef<Song> song = mp_get_song(db_get_random_song());
            if (song == nullptr)
            {
                SPDLOG_WARN("Cannot autoplay with no songs in DB");
                return;
            }
            mp_ctx.autoplay_queue.push_back(std::move(song));
        }
    }
    mp_ctx.autoplay = !mp_ctx.autoplay;
    if (mp_ctx.autoplay && mp_ctx.current_song == nullptr)
        mp_play_next();
}

void mp_update_volume(float volume)
{
    if (mp_ctx.mute)
        ma_sound_set_volume(&ctx.current_song_sound, 0.0f);
    else
        ma_sound_set_volume(&ctx.current_song_sound, mp_ctx.volume * volume);
}

void mp_toggle_mute()
{
    mp_ctx.mute = !mp_ctx.mute;
    if (mp_ctx.current_song != nullptr)
        mp_update_volume(mp_ctx.current_song->volume);
}

void mp_update_cursor()
{
    ma_sound_seek_to_second(&ctx.current_song_sound, mp_ctx.current_song_cursor);
}

static void sort_or_shuffle_group_queue()
{
    if (mp_ctx.shuffle) {
        std::shuffle(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), ctx.mt);
    } else {
        std::sort(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), [](const SongTrack& pair1, const SongTrack& pair2) 
            {
                return pair1.track < pair2.track;
            });
    }
}

static void add_playlist_songs_to_group_queue(PlaylistID playlist_id)
{
    mp_ctx.group_queue.clear();
    std::vector<SongTrackID> song_tracks = mp_get_songs_from_playlist(playlist_id);
    for (SongTrackID song_track : song_tracks)
    {
        LruCacheRef<Song> song = mp_get_song(song_track.song_id);
        mp_ctx.group_queue.emplace_back(std::move(song), song_track.track);
    }
    sort_or_shuffle_group_queue();
}

static void add_album_songs_to_group_queue(AlbumID album_id)
{
    mp_ctx.group_queue.clear();
    std::vector<SongTrackID> song_tracks = mp_get_songs_from_album(album_id);
    for (SongTrackID song_track : song_tracks)
    {
        LruCacheRef<Song> song = mp_get_song(song_track.song_id);
        mp_ctx.group_queue.emplace_back(std::move(song), song_track.track);
    }
    sort_or_shuffle_group_queue();
}

void mp_play_playlist(PlaylistID playlist_id)
{
    mp_ctx.playing_group = true;
    mp_ctx.group_is_album = false;
    mp_ctx.group_id = playlist_id;
    mp_ctx.queue.clear();
    add_playlist_songs_to_group_queue(playlist_id);
    mp_play_next();
}

void mp_play_album(AlbumID album_id)
{
    mp_ctx.playing_group = true;
    mp_ctx.group_is_album = true;
    mp_ctx.group_id = album_id;
    mp_ctx.queue.clear();
    add_album_songs_to_group_queue(album_id);
    mp_play_next();
}

void mp_get_song_lyrics(SongID song_id, char* lyrics_buffer, size_t lyrics_buffer_size)
{
    db_get_song_lyrics(song_id, lyrics_buffer, lyrics_buffer_size);
}

void mp_update_song_lyrics(SongID song_id, const char* lyrics)
{
    db_update_song_lyrics(song_id, lyrics);
    if (mp_ctx.current_song != nullptr && mp_ctx.current_song->id == song_id)
        snprintf(mp_ctx.current_song_lyrics, sizeof(mp_ctx.current_song_lyrics), "%s", lyrics);
}

void mp_update_song_art(SongID song_id, Art* art)
{
    db_update_song_art(song_id, art);
}

void mp_update_song_volume(SongID song_id, float volume)
{
    db_update_song_volume(song_id, volume);
    if (mp_ctx.current_song != nullptr && mp_ctx.current_song->id == song_id)
        mp_update_volume(volume);
}

void mp_update_song(Song& song)
{
    db_update_song(song);
    LruCacheRef<Song> song_cache_ref = mp_ctx.songs.get(song.id);
    if (song_cache_ref != nullptr)
    {
        song_cache_ref->title = std::move(song.title);
        song_cache_ref->volume = song.volume;
        song_cache_ref->path = std::move(song.path);
        song_cache_ref->year = song.year;
    }
}

void mp_update_album(AlbumID album_id, const char* name)
{
    db_update_album(album_id, name);
    LruCacheRef<Album> album = mp_ctx.albums.get(album_id);
    if (album != nullptr)
    {
        album->name = name;
    }
}

void mp_update_artist(ArtistID artist_id, const char* name)
{
    db_update_artist(artist_id, name);
    LruCacheRef<Artist> artist = mp_ctx.artists.get(artist_id);
    if (artist != nullptr)
    {
        artist->name = name;
    }
}

void mp_update_song_artist(SongID song_id, ArtistID artist_id)
{
    (void)song_id; (void)artist_id;
}

void mp_update_song_album(SongID song_id, AlbumID album_id)
{
    (void)song_id; (void)album_id;
}

void mp_update_album_artist(AlbumID album_id, ArtistID artist_id)
{
    (void)album_id; (void)artist_id;
}

LruCacheRef<Artist> mp_create_artist()
{
    ArtistID artist_id = db_create_default_artist();
    db_update_artist(artist_id, std::format("Unnamed Artist #{}", artist_id).c_str());
    return mp_get_artist(artist_id);
}

LruCacheRef<Album> mp_create_album()
{
    AlbumID album_id = db_create_default_album();
    db_update_album(album_id, std::format("Unnamed Album #{}", album_id).c_str());
    return mp_get_album(album_id);
}

LruCacheRef<Playlist> mp_create_playlist()
{
    PlaylistID playlist_id = db_create_default_playlist();
    db_update_playlist(playlist_id, std::format("Unnamed Playlist #{}", playlist_id).c_str());
    return mp_get_playlist(playlist_id);
}

void mp_pin_song(SongID song_id)
{
    if (!db_create_pin(song_id, MPUID_SONG))
        return;

    mp_ctx.pins.push_back(Pin{
                .song = mp_get_song(song_id),
                .album = nullptr,
                .artist = nullptr,
                .playlist = nullptr,
                .type = MPUID_SONG,
                .order = static_cast<int>(mp_ctx.pins.size())
            });
}

void mp_pin_album(AlbumID album_id)
{
    if (!db_create_pin(album_id, MPUID_ALBUM))
        return;
    
    mp_ctx.pins.push_back(Pin{
                .song = nullptr,
                .album = mp_get_album(album_id),
                .artist = nullptr,
                .playlist = nullptr,
                .type = MPUID_ALBUM,
                .order = static_cast<int>(mp_ctx.pins.size())
            });
}

void mp_pin_artist(ArtistID artist_id)
{
    if (!db_create_pin(artist_id, MPUID_ARTIST))
        return;

    mp_ctx.pins.push_back(Pin{
                .song = nullptr,
                .album = nullptr,
                .artist = mp_get_artist(artist_id),
                .playlist = nullptr,
                .type = MPUID_ARTIST,
                .order = static_cast<int>(mp_ctx.pins.size())
            });
}

void mp_pin_playlist(PlaylistID playlist_id)
{
    if (!db_create_pin(playlist_id, MPUID_PLAYLIST))
        return;

    mp_ctx.pins.push_back(Pin{
                .song = nullptr,
                .album = nullptr,
                .artist = nullptr,
                .playlist = mp_get_playlist(playlist_id),
                .type = MPUID_PLAYLIST,
                .order = static_cast<int>(mp_ctx.pins.size())
            });
}

SearchResult<Song> mp_search_songs(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<SongID> search_result_id = db_search_songs(search_query, results_per_page, page_num);
    SearchResult<Song> search_result{};
    search_result.page_num = search_result_id.page_num;
    search_result.num_pages = search_result_id.num_pages;
    search_result.results_per_page = search_result_id.results_per_page;
    search_result.num_results = search_result_id.num_results;
    for (SongID song_id : search_result_id.entries)
    {
        LruCacheRef<Song> song = mp_get_song(song_id);
        assert(song != nullptr);
        search_result.entries.push_back(std::move(song));
    }
    return search_result;
}

SearchResult<Album> mp_search_albums(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<AlbumID> search_result_id = db_search_albums(search_query, results_per_page, page_num);
    SearchResult<Album> search_result{};
    search_result.page_num = search_result_id.page_num;
    search_result.num_pages = search_result_id.num_pages;
    search_result.results_per_page = search_result_id.results_per_page;
    search_result.num_results = search_result_id.num_results;
    for (AlbumID album_id : search_result_id.entries)
    {
        LruCacheRef<Album> album = mp_get_album(album_id);
        assert(album != nullptr);
        search_result.entries.push_back(std::move(album));
    }
    return search_result;
}

SearchResult<Artist> mp_search_artists(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<ArtistID> search_result_id = db_search_artists(search_query, results_per_page, page_num);
    SearchResult<Artist> search_result{};
    search_result.page_num = search_result_id.page_num;
    search_result.num_pages = search_result_id.num_pages;
    search_result.results_per_page = search_result_id.results_per_page;
    search_result.num_results = search_result_id.num_results;
    for (ArtistID artist_id : search_result_id.entries)
    {
        LruCacheRef<Artist> artist = mp_get_artist(artist_id);
        assert(artist != nullptr);
        search_result.entries.push_back(std::move(artist));
    }
    return search_result;
}

SearchResult<Playlist> mp_search_playlists(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<PlaylistID> search_result_id = db_search_playlists(search_query, results_per_page, page_num);
    SearchResult<Playlist> search_result{};
    search_result.page_num = search_result_id.page_num;
    search_result.num_pages = search_result_id.num_pages;
    search_result.results_per_page = search_result_id.results_per_page;
    search_result.num_results = search_result_id.num_results;
    for (PlaylistID playlist_id : search_result_id.entries)
    {
        LruCacheRef<Playlist> playlist = mp_get_playlist(playlist_id);
        assert(playlist != nullptr);
        search_result.entries.push_back(std::move(playlist));
    }
    return search_result;
}

SearchResult<Song> mp_get_paginated_songs_from_artist(ArtistID artist_id, int results_per_page, int page_num)
{
    SearchResultID<SongID> search_result_id = db_search_songs_from_artist(artist_id, results_per_page, page_num);
    SearchResult<Song> search_result{};
    search_result.page_num = search_result_id.page_num;
    search_result.num_pages = search_result_id.num_pages;
    search_result.results_per_page = search_result_id.results_per_page;
    search_result.num_results = search_result_id.num_results;
    for (SongID song_id : search_result_id.entries)
    {
        LruCacheRef<Song> song = mp_get_song(song_id);
        assert(song != nullptr);
        search_result.entries.push_back(std::move(song));
    }
    return search_result;
}

[[maybe_unused]] void mp_song_update(SongID song_id, const char* title, const char* artist, const char* album, const char* cover_path)
{
    (void)song_id;
    (void)title;
    (void)artist;
    (void)album;
    (void)cover_path;
    //Song* song = mp_get_song_from_id(song_id);
    //int album_id = mp_get_album_id_from_song_id(song_id);
    //db_update_song(song_id, title, song->path.c_str(), song->length);

    //db_update_song_artist(song_id, artist);
    //db_update_song_album(song_id, album);

    //song->title = std::string{title};
    // change artistsong
    // change albumsong
    return;
}

LruCacheRef<Song> mp_get_song(SongID song_id)
{
    if (song_id == INVALID_ID)
        return nullptr;
    LruCacheRef<Song> song = mp_ctx.songs.get(song_id);
    if (song == nullptr)
        song = mp_ctx.songs.put(song_id, db_get_song_info(song_id));
    return song;
}

LruCacheRef<Album> mp_get_album(AlbumID album_id)
{
    if (album_id == INVALID_ID)
        return nullptr;
    LruCacheRef<Album> album = mp_ctx.albums.get(album_id);
    if (album == nullptr)
        album = mp_ctx.albums.put(album_id, db_get_album_info(album_id));
    return album;
}

LruCacheRef<Artist> mp_get_artist(ArtistID artist_id)
{
    if (artist_id == INVALID_ID)
        return nullptr;
    LruCacheRef<Artist> artist = mp_ctx.artists.get(artist_id);
    if (artist == nullptr)
        artist = mp_ctx.artists.put(artist_id, db_get_artist_info(artist_id));
    return artist;
}

LruCacheRef<Playlist> mp_get_playlist(PlaylistID playlist_id)
{
    LruCacheRef<Playlist> playlist = mp_ctx.playlists.get(playlist_id);
    if (playlist == nullptr)
        playlist = mp_ctx.playlists.put(playlist_id, db_get_playlist_info(playlist_id));
    return playlist;
}

LruCacheRef<Artist> mp_get_artist_from_song(SongID song_id)
{
    LruCacheRef<ArtistID> artist_id = mp_ctx.song_artist.get(song_id);
    if (artist_id == nullptr)
        artist_id = mp_ctx.song_artist.put(song_id, db_get_artist_from_song(song_id));
    return mp_get_artist(*artist_id);
}

LruCacheRef<Album> mp_get_album_from_song(SongID song_id)
{
    LruCacheRef<AlbumID> album_id = mp_ctx.song_album.get(song_id);
    if (album_id == nullptr)
        album_id = mp_ctx.song_album.put(song_id, db_get_album_from_song(song_id));
    return mp_get_album(*album_id);
}

LruCacheRef<Artist> mp_get_artist_from_album(AlbumID album_id)
{
    LruCacheRef<ArtistID> artist_id = mp_ctx.album_artist.get(album_id);
    if (artist_id == nullptr)
        artist_id = mp_ctx.album_artist.put(album_id, db_get_artist_from_album(album_id));
    return mp_get_artist(*artist_id);
}

std::vector<SongTrackID> mp_get_songs_from_album(AlbumID album_id)
{
    return db_get_songs_from_album(album_id);
}

std::vector<SongTrackID> mp_get_songs_from_playlist(PlaylistID playlist_id)
{
    return db_get_songs_from_playlist(playlist_id);
}

std::vector<SongID> mp_get_songs_from_artist(ArtistID artist_id)
{
    return db_get_songs_from_artist(artist_id);
}

std::vector<AlbumID> mp_get_albums_from_artist(ArtistID artist_id)
{
    return db_get_albums_from_artist(artist_id);
}

std::vector<LruCacheRef<Playlist>> mp_get_playlists()
{
    std::vector<PlaylistID> playlist_ids = db_get_playlists();
    std::vector<LruCacheRef<Playlist>> playlists{};
    for (PlaylistID playlist_id : playlist_ids)
        playlists.emplace_back(mp_get_playlist(playlist_id));
    return playlists;
}

void mp_play_previous()
{
    if (mp_ctx.song_history.size() == 0)
        return;

    if (mp_ctx.current_song != nullptr)
        mp_ctx.queue.push_front(std::move(mp_ctx.current_song));

    SongID song_id = mp_ctx.song_history.back();
    mp_ctx.song_history.pop_back();
    mp_play_song(song_id);
}

static void play_next_group_song()
{
    if (mp_ctx.group_queue.size() == 0) 
    {
        if (mp_ctx.loop_mode == LOOP_NONE) {
            mp_ctx.playing_group = false;
            mp_play_next();
            return;
        }

        if (mp_ctx.group_is_album)
            add_album_songs_to_group_queue(mp_ctx.group_id);
        else
            add_playlist_songs_to_group_queue(mp_ctx.group_id);

        if (mp_ctx.group_queue.size() == 0) {
            SPDLOG_WARN("Tried to play group with no songs");
            mp_ctx.playing_group = false;
            mp_play_next();
            return;
        }
    }
    const int song_id = mp_ctx.group_queue.front().song->id;
    mp_ctx.group_queue.pop_front();
    mp_play_song(song_id);
}

void mp_play_next()
{
    if (mp_ctx.current_song != nullptr)
        mp_ctx.song_history.push_back(mp_ctx.current_song->id);

    if (mp_ctx.loop_mode == LOOP_TRACK && mp_ctx.current_song != nullptr) {
        mp_play_song(mp_ctx.current_song->id);
        return;
    }
    mp_ctx.paused = false;
    if (mp_ctx.queue.size() == 0) {
        if (mp_ctx.playing_group) {
            play_next_group_song();
        } else if (mp_ctx.autoplay) {
            assert(mp_ctx.autoplay_queue.size() > 0);
            LruCacheRef<Song> song = std::move(mp_ctx.autoplay_queue.front());
            mp_ctx.autoplay_queue.pop_front();
            mp_ctx.autoplay_queue.push_back(mp_get_song(db_get_random_song()));
            mp_play_song(song->id);
        } else if (ctx.current_song_loaded) {
            mp_ctx.current_song = nullptr;
            mp_ctx.current_song_artist = nullptr;
            mp_ctx.current_song_album = nullptr;
            mp_ctx.current_song_lyrics[0] = '\0';
            ma_sound_uninit(&ctx.current_song_sound);
            ctx.current_song_loaded = false;
        }
    } else {
        LruCacheRef<Song> song = std::move(mp_ctx.queue.front());
        mp_ctx.queue.pop_front();
        mp_play_song(song->id);
    }
}

Art mp_get_song_art(SongID song_id)
{
    return db_get_song_art(song_id);
}

Art mp_get_album_art(AlbumID album_id)
{
    return db_get_album_art(album_id);
}

Art mp_get_art(MPUID uid)
{
    switch (db_get_uid_type(uid))
    {
        case MPUID_SONG:
            return db_get_song_art(db_get_song_uid(uid));
        case MPUID_ALBUM:
            return db_get_album_art(db_get_album_uid(uid));
        default:
            return {};
    }
}

void mp_free_art(Art* art)
{
    db_free_art(art);
}

void mp_queue_clear()
{
    mp_ctx.queue.clear();
    mp_ctx.group_queue.clear();
    mp_ctx.playing_group = false;
    mp_ctx.group_id = INVALID_ID;
}

MPUID mp_get_song_uid(SongID song_id)
{
    return db_get_song_uid(song_id);
}

MPUID mp_get_playlist_uid(PlaylistID playlist_id)
{
    return db_get_playlist_uid(playlist_id);
}

MPUID mp_get_album_uid(AlbumID album_id)
{
    return db_get_album_uid(album_id);
}

MPUID mp_get_artist_uid(ArtistID artist_id)
{
    return db_get_artist_uid(artist_id);
}

MPUID mp_get_uid(int id, MPUIDEnum type)
{
    return db_get_uid(id, type);
}

MPUIDEnum mp_get_uid_type(MPUID uid)
{
    return db_get_uid_type(uid);
}

int mp_get_id(MPUID uid)
{
    return db_get_id(uid);
}

void mp_change_db(const std::string& db_path)
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    ctx.current_song_loaded = false;

    mp_ctx.current_song.release();
    mp_ctx.current_song_artist.release();
    mp_ctx.current_song_album.release();
    mp_ctx.queue.clear();
    mp_ctx.autoplay_queue.clear();
    mp_ctx.group_queue.clear();
    mp_ctx.song_history.clear();
    mp_ctx.pins.clear();
    mp_ctx.added_songs.clear();
    mp_ctx.added_artists.clear();
    mp_ctx.added_albums.clear();
    mp_ctx.playing_group = false;
    mp_ctx.autoplay = false;

    mp_ctx.songs.clear();
    mp_ctx.albums.clear();
    mp_ctx.artists.clear();
    mp_ctx.playlists.clear();
    mp_ctx.song_artist.clear();
    mp_ctx.song_album.clear();
    mp_ctx.album_artist.clear();

    db_switch(db_path);
}

void mp_init()
{
    ctx.mt.seed(std::chrono::steady_clock::now().time_since_epoch().count());

    mp_ctx.volume = 1.0f;
    mp_ctx.shuffle = true;
    db_init();
    ma_result res;
    res = ma_engine_init(NULL, &ctx.engine);
    if (res != MA_SUCCESS)
        exit(1);

    std::vector<PinID> pin_ids = db_get_pins();
    mp_ctx.pins.resize(pin_ids.size());
    std::transform(pin_ids.begin(), pin_ids.end(), mp_ctx.pins.begin(),
        [](PinID& pin_id) -> Pin
        {
            Pin pin{};
            int id = mp_get_id(pin_id.uid);
            pin.type = mp_get_uid_type(pin_id.uid);
            pin.order = pin_id.order;
            switch (pin.type)
            {
                case MPUID_SONG:
                    pin.song = mp_get_song(id);
                    break;
                case MPUID_ALBUM:
                    pin.album = mp_get_album(id);
                    break;
                case MPUID_ARTIST:
                    pin.artist = mp_get_artist(id);
                    break;
                case MPUID_PLAYLIST:
                    pin.playlist = mp_get_playlist(id);
                    break;
            }
            return pin;
        });

    mp_ctx.num_songs = db_get_num_songs();
    mp_ctx.num_albums = db_get_num_albums();
    mp_ctx.num_artists = db_get_num_artists();
    mp_ctx.num_playlists = db_get_num_playlists();
}

void mp_cleanup()
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    ma_engine_uninit(&ctx.engine);
}

void mp_update()
{
    if (mp_ctx.current_song != nullptr) {
        ma_sound_get_cursor_in_seconds(&ctx.current_song_sound, &mp_ctx.current_song_cursor);
    }
    if (ctx.song_ended) {
        mp_play_next();
        ctx.song_ended = false;
    }
}

