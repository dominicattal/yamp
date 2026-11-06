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
    DBConn conn;

    std::mt19937 mt;
    ma_engine engine;
    ma_sound current_song_sound;
    bool current_song_loaded;
    bool song_ended;
};

MPContext mp_ctx;
static MPContextInternal ctx;

static bool test_pin_with_uid(Pin* pin, MPUID uid)
{
    MPUIDEnum type = mp_get_uid_type(uid);
    int id = mp_get_id(uid);
    if (pin->type != type)
        return false;
    if (pin->type == MPUID_SONG && id == pin->song->id)
        return true;
    if (pin->type == MPUID_ALBUM && id == pin->album->id)
        return true;
    if (pin->type == MPUID_ARTIST && id == pin->artist->id)
        return true;
    if (pin->type == MPUID_PLAYLIST && id == pin->playlist->id)
        return true;
    return false;
}

static void add_song_from_path(DBConn& conn, const std::string& song_path)
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

    SongID song_id = conn.get_song_id(song_path.c_str());
    if (song_id != INVALID_ID)
    {
        SPDLOG_WARN("Song id {} already exists", song_id);
        return;
    }

    auto set_song_art_data = [&mp3_file_ref]() -> Art
        {
            Art art{};
            TagLib::List<TagLib::VariantMap> props = mp3_file_ref.complexProperties("PICTURE");
            if (props.isEmpty())
                return art;
            const TagLib::VariantMap& map = props.front();
            if (!map.contains("data"))
                return art;
            int input_w, input_h, nc;
            const TagLib::ByteVector data = map["data"].toByteVector();
            uint8_t* raw_art_data = stbi_load_from_memory(reinterpret_cast<const uint8_t*>(data.data()), data.size(), &input_w, &input_h, &nc, 4);
            art.data = stbir_resize_uint8_linear(
                        raw_art_data, input_w, input_h, 0,
                        NULL, LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE, 0,
                        STBIR_4CHANNEL
                    );
            art.size = LARGE_COVER_ART_SIZE * LARGE_COVER_ART_SIZE * 4;
            stbi_image_free(raw_art_data);
            return art;
        };

    Song song{};
    song.name = tag->title().to8Bit(true);
    song.year = tag->year();
    song.length = properties->lengthInMilliseconds() / 1000.0;
    song.path = song_path;
    song.id = conn.create_song(song);

    if (song.id == INVALID_ID)
        return;

    Album album{};
    album.name = tag->album().to8Bit(true);
    album.year = tag->year();

    Artist artist{};
    artist.name = tag->artist().to8Bit(true);

    int track = tag->track();

    Art art = set_song_art_data();
    conn.update_song_art(song.id, art);

    mp_ctx.added_songs.push_back(song.id);
    SPDLOG_INFO("Created song {} {}", song.id, song.name);

    if (album.name.size() > 0) 
    {
        album.id = conn.get_album_id(album.name.c_str());
        if (album.id == INVALID_ID) 
        {
            bool loaded_art = false;
            std::vector filenames{"cover.jpg", "cover.png"};
            for (const char* cover_path : filenames)
            {
                std::filesystem::path fs_song_path = std::filesystem::path(song_path).replace_filename(cover_path);
                if (std::filesystem::exists(fs_song_path))
                {
                    Art art{};

                    album.id = conn.create_album(album);

                    int input_w, input_h, nc;
                    uint8_t* raw_art_data = stbi_load(fs_song_path.string().c_str(), &input_w, &input_h, &nc, 4);
                    art.data = stbir_resize_uint8_linear(
                                raw_art_data, input_w, input_h, 0,
                                NULL, LARGE_COVER_ART_SIZE, LARGE_COVER_ART_SIZE, 0,
                                STBIR_4CHANNEL
                            );
                    art.size = LARGE_COVER_ART_SIZE * LARGE_COVER_ART_SIZE * 4;
                    stbi_image_free(raw_art_data);

                    conn.update_album_art(album.id, art);

                    loaded_art = true;

                    SPDLOG_INFO("Loaded album {} art {}", album.name.c_str(), fs_song_path.string());

                    break;
                }
            }

            if (!loaded_art)
            {
                album.id = conn.create_album(album);
                Art art = mp_get_song_art(song.id);
                conn.update_album_art(album.id, art);
                SPDLOG_INFO("Loaded album {} art from song {}", album.id, song.id);
            }

            album.id = conn.get_album_id(album.name.c_str());
            mp_ctx.added_albums.push_back(album.id);
            SPDLOG_INFO("Created album {} {}", album.id, album.name);
        }
        conn.create_album_song(album.id, song.id, track);
    }

    if (artist.name.size() > 0) 
    {
        artist.id = conn.get_artist_id(artist.name.c_str());
        if (artist.id == INVALID_ID)
        {
            conn.create_artist(artist);
            artist.id = conn.get_artist_id(artist.name.c_str());
            mp_ctx.added_artists.push_back(artist.id);
            SPDLOG_INFO("Created artist {} {}", artist.id, artist.name);
        }

        conn.create_artist_song(artist.id, song.id);
    }

    if (album.id != 0 && artist.id != 0 && !conn.artist_album_exists(artist.id, album.id))
        conn.create_artist_album(artist.id, album.id);
}

void mp_add_song(std::string&& song_path)
{
    DBConn conn{};
    mp_ctx.adding_songs = true;
    conn.begin_transaction();
    add_song_from_path(conn, song_path);
    conn.commit_transaction();
    mp_ctx.adding_songs = false;
}

void mp_add_songs(std::vector<std::string>&& song_paths)
{
    DBConn conn{};
    mp_ctx.adding_songs = true;
    conn.begin_transaction();
    for (std::string& song_path : song_paths)
        add_song_from_path(conn, song_path);
    conn.commit_transaction();
    mp_ctx.adding_songs = false;
}

void mp_recursive_add_songs(const std::string& folder_paths)
{
    namespace fs = std::filesystem;

    std::stringstream folder_paths_stream{folder_paths};
    fs::directory_options options = fs::directory_options::follow_directory_symlink;
    std::vector<std::string> separated_paths{};
    std::string folder_path{};

    mp_ctx.num_songs_to_add = 0;
    mp_ctx.num_songs_added = 0;
    mp_ctx.adding_songs = true;

    while (std::getline(folder_paths_stream, folder_path, '\n'))
    {
        if (!fs::exists(std::filesystem::path{folder_path}))
            continue;

        fs::recursive_directory_iterator entries = fs::recursive_directory_iterator(folder_path, options);
        for (auto const& dir_entry : entries)
        {
            (void)dir_entry;
            mp_ctx.num_songs_to_add++;
        }
        separated_paths.push_back(std::move(folder_path));
    }

    DBConn conn{};
    conn.begin_transaction();
    for (std::string& folder_path : separated_paths)
    {
        fs::recursive_directory_iterator entries = fs::recursive_directory_iterator(folder_path, options);
        for (auto const& dir_entry : entries)
        {
            {
                std::lock_guard lock{mp_ctx.adding_songs_mutex};
                mp_ctx.adding_songs_current_path = dir_entry.path().string();
            }
            add_song_from_path(conn, dir_entry.path().string());
            mp_ctx.num_songs_added++;
        }
    }
    conn.commit_transaction();

    mp_ctx.adding_songs = false;
}

void mp_reset_loaded_songs()
{
    mp_ctx.adding_songs = false;
    mp_ctx.added_songs.clear();
    mp_ctx.added_artists.clear();
    mp_ctx.added_albums.clear();
}

void mp_add_song_to_playlist(SongID song_id, PlaylistID playlist_id)
{
    std::vector<SongTrackID> tracks = ctx.conn.get_songs_from_playlist(playlist_id);
    int track = tracks.size() + 1;
    ctx.conn.create_playlist_song(playlist_id, song_id, track);
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
    mp_ctx.current_song.song = std::move(song);
    mp_ctx.current_song.album = mp_get_album_from_song(song_id);
    mp_ctx.current_song.artist = mp_get_artist_from_song(song_id);
    if (mp_ctx.current_song.song != nullptr)
        mp_update_volume(mp_ctx.current_song.song->volume);

    ctx.conn.get_song_lyrics(song_id, mp_ctx.current_song_lyrics, sizeof(mp_ctx.current_song_lyrics));
}

void mp_queue_song(SongID song_id)
{
    mp_ctx.queue.emplace_back(
                mp_get_song(song_id),
                mp_get_album_from_song(song_id),
                mp_get_artist_from_song(song_id)
            );
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
            std::sort(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), [](const auto& v1, const auto& v2) {
                    return v1.track < v2.track;
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
            LruCacheRef<Song> song = mp_get_song(ctx.conn.get_random_song());
            if (song == nullptr)
            {
                SPDLOG_WARN("Cannot autoplay with no songs in DB");
                return;
            }
            SongID song_id = song->id;
            mp_ctx.autoplay_queue.emplace_back(
                        std::move(song),
                        mp_get_album_from_song(song_id),
                        mp_get_artist_from_song(song_id)
                    );
        }
    }
    mp_ctx.autoplay = !mp_ctx.autoplay;
    if (mp_ctx.autoplay && mp_ctx.current_song.song == nullptr)
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
    if (mp_ctx.current_song.song != nullptr)
        mp_update_volume(mp_ctx.current_song.song->volume);
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
        std::sort(mp_ctx.group_queue.begin(), mp_ctx.group_queue.end(), [](const auto& v1, const auto& v2) 
            {
                return v1.track < v2.track;
            });
    }
}

static void add_playlist_songs_to_group_queue(PlaylistID playlist_id)
{
    mp_ctx.group_queue.clear();
    std::vector<SongTrackID> song_tracks = mp_get_songs_from_playlist(playlist_id);
    for (SongTrackID song_track : song_tracks)
    {
        SongAlbumArtistTrack song;
        song.song = mp_get_song(song_track.song_id);
        song.album = mp_get_album_from_song(song_track.song_id);
        song.artist = mp_get_artist_from_song(song_track.song_id);
        song.track = song_track.track;
        mp_ctx.group_queue.push_back(std::move(song));
    }
    sort_or_shuffle_group_queue();
}

static void add_album_songs_to_group_queue(AlbumID album_id)
{
    mp_ctx.group_queue.clear();
    std::vector<SongTrackID> song_tracks = mp_get_songs_from_album(album_id);
    for (SongTrackID song_track : song_tracks)
    {
        SongAlbumArtistTrack song;
        song.song = mp_get_song(song_track.song_id);
        song.album = mp_get_album_from_song(song_track.song_id);
        song.artist = mp_get_artist_from_song(song_track.song_id);
        song.track = song_track.track;
        mp_ctx.group_queue.push_back(std::move(song));
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
    ctx.conn.get_song_lyrics(song_id, lyrics_buffer, lyrics_buffer_size);
}

void mp_update_song_lyrics(SongID song_id, const char* lyrics)
{
    ctx.conn.update_song_lyrics(song_id, lyrics);
    if (mp_ctx.current_song.song != nullptr && mp_ctx.current_song.song->id == song_id)
        snprintf(mp_ctx.current_song_lyrics, sizeof(mp_ctx.current_song_lyrics), "%s", lyrics);
}

void mp_update_song_art(SongID song_id, Art& art)
{
    ctx.conn.update_song_art(song_id, art);
}

void mp_update_album_art(AlbumID album_id, Art& art)
{
    ctx.conn.update_album_art(album_id, art);
}

void mp_update_artist_art(ArtistID artist_id, Art& art)
{
    ctx.conn.update_artist_art(artist_id, art);
}

void mp_update_song_volume(SongID song_id, float volume)
{
    ctx.conn.update_song_volume(song_id, volume);
    if (mp_ctx.current_song.song != nullptr && mp_ctx.current_song.song->id == song_id)
        mp_update_volume(volume);
}

void mp_update_song(Song& song)
{
    TagLib::FileRef mp3_file_ref(song.path.c_str());
    if (mp3_file_ref.isNull() || !mp3_file_ref.tag()) {
        SPDLOG_ERROR("Could not read {}", song.path);
        return;
    }
    TagLib::Tag* tag = mp3_file_ref.tag();
    assert(tag);
    TagLib::AudioProperties* properties = mp3_file_ref.audioProperties();
    song.length = properties->lengthInMilliseconds() / 1000.0;

    ctx.conn.update_song(song);
    LruCacheRef<Song> song_cache_ref = mp_ctx.songs.get(song.id);
    if (song_cache_ref != nullptr)
    {
        song_cache_ref->name = std::move(song.name);
        song_cache_ref->volume = song.volume;
        song_cache_ref->path = std::move(song.path);
        song_cache_ref->year = song.year;
        song_cache_ref->length = song.length;
    }
}

void mp_update_album(Album& album)
{
    ctx.conn.update_album(album);
    LruCacheRef<Album> album_cache_ref = mp_ctx.albums.get(album.id);
    if (album_cache_ref != nullptr)
    {
        album_cache_ref->name = std::move(album.name);
        album_cache_ref->year = album.year;
    }
}

void mp_update_artist(Artist& artist)
{
    ctx.conn.update_artist(artist);
    LruCacheRef<Artist> artist_cache_ref = mp_ctx.artists.get(artist.id);
    if (artist_cache_ref != nullptr)
        artist_cache_ref->name = artist.name;
}


void mp_update_playlist(Playlist& playlist)
{
    ctx.conn.update_playlist(playlist);
    LruCacheRef<Playlist> playlist_cache_ref = mp_ctx.playlists.get(playlist.id);
    if (playlist_cache_ref != nullptr)
        playlist_cache_ref->name = playlist.name;
}

void mp_update_song_artist(SongID song_id, ArtistID artist_id)
{
    ctx.conn.update_song_artist(song_id, artist_id);
    LruCacheRef<ArtistID> artist_id_cache_ref = mp_ctx.song_artist.get(song_id);
    if (artist_id_cache_ref != nullptr)
        *artist_id_cache_ref = artist_id;
}

void mp_update_song_album(SongID song_id, AlbumID album_id, int track)
{
    ctx.conn.update_song_album(song_id, album_id, track);
    LruCacheRef<AlbumID> album_id_cache_ref = mp_ctx.song_album.get(song_id);
    if (album_id_cache_ref != nullptr)
        *album_id_cache_ref = album_id;
}

void mp_update_album_artist(AlbumID album_id, ArtistID artist_id)
{
    ctx.conn.update_album_artist(album_id, artist_id);
    LruCacheRef<ArtistID> artist_id_cache_ref = mp_ctx.album_artist.get(album_id);
    if (artist_id_cache_ref != nullptr)
        *artist_id_cache_ref = artist_id;
}

void mp_update_playlist_art(PlaylistID playlist_id, Art& art)
{
    ctx.conn.update_playlist_art(playlist_id, art);
}

void mp_merge_artists(ArtistID dst_artist_id, ArtistID src_artist_id)
{
    {
        std::lock_guard lock{mp_ctx.adding_songs_mutex};
        mp_ctx.added_artists.erase(
            std::remove_if(mp_ctx.added_artists.begin(), mp_ctx.added_artists.end(),
                [&src_artist_id](ArtistID artist_id)
                {
                    return artist_id == src_artist_id;
                }),
            mp_ctx.added_artists.end()
            );
    }

    mp_ctx.pins.erase(
        std::remove_if(mp_ctx.pins.begin(), mp_ctx.pins.end(),
            [&src_artist_id](Pin& pin)
            {
                return pin.type == MPUID_ARTIST && pin.artist->id == src_artist_id;
            }),
        mp_ctx.pins.end()
        );

    bool update_current_song_artist{};
    if (mp_ctx.current_song.artist != nullptr && mp_ctx.current_song.artist->id == src_artist_id)
    {
        mp_ctx.current_song.artist.release();
        update_current_song_artist = true;
    }

    mp_ctx.artists.remove(src_artist_id);
    mp_ctx.song_artist.clear();
    mp_ctx.album_artist.clear();

    ctx.conn.begin_transaction();
    ctx.conn.delete_pin(mp_get_artist_uid(src_artist_id));
    ctx.conn.merge_artists(dst_artist_id, src_artist_id);
    ctx.conn.commit_transaction();

    if (update_current_song_artist)
        mp_ctx.current_song.artist = mp_get_artist(dst_artist_id);
}

void mp_update_album_songs_year(AlbumID album_id, int year)
{
    ctx.conn.update_album_songs_year(album_id, year);
    std::vector<SongTrackID> song_tracks = mp_get_songs_from_album(album_id);
    for (auto [song_id, track] : song_tracks)
    {
        LruCacheRef<Song> song_cache_ref = mp_ctx.songs.get(song_id);
        if (song_cache_ref != nullptr)
            song_cache_ref->year = year;
    }
}

void mp_update_album_songs_art(AlbumID album_id)
{
    ctx.conn.update_album_songs_art(album_id);
}

LruCacheRef<Artist> mp_create_artist()
{
    Artist artist{};
    artist.id = ctx.conn.create_default_artist();
    artist.name = std::format("Unnamed Artist #{}", artist.id);
    ctx.conn.update_artist(artist);
    return mp_get_artist(artist.id);
}

LruCacheRef<Song> mp_create_song()
{
    Song song{};
    song.id = ctx.conn.create_default_song();
    song.name = std::format("Unnamed Song #{}", song.id);
    ctx.conn.update_song(song);
    return mp_get_song(song.id);
}

LruCacheRef<Album> mp_create_album()
{
    Album album{};
    album.id = ctx.conn.create_default_album();
    album.name = std::format("Unnamed Album #{}", album.id);
    ctx.conn.update_album(album);
    return mp_get_album(album.id);
}

LruCacheRef<Playlist> mp_create_playlist()
{
    Playlist playlist{};
    playlist.id = ctx.conn.create_default_playlist();
    playlist.name = std::format("Unnamed Playlist #{}", playlist.id);
    ctx.conn.update_playlist(playlist);
    return mp_get_playlist(playlist.id);
}

void mp_update_pin(MPUID pin_uid)
{
    uint64_t last_accessed = std::chrono::steady_clock::now().time_since_epoch().count();

    ctx.conn.update_pin(pin_uid, last_accessed);

    std::for_each(mp_ctx.pins.begin(), mp_ctx.pins.end(),
            [&pin_uid, &last_accessed](Pin& pin)
            {
                if (pin.uid == pin_uid)
                    pin.last_accessed = last_accessed;
            });

    std::sort(mp_ctx.pins.begin(), mp_ctx.pins.end(),
            [](const Pin& p1, const Pin& p2)
            {
                return p1.last_accessed > p2.last_accessed;
            });
}

bool mp_uid_pinned(MPUID uid)
{
    for (Pin& pin : mp_ctx.pins)
        if (test_pin_with_uid(&pin, uid))
            return true;
    return false;
}

bool mp_toggle_pin_song(SongID song_id)
{
    MPUID uid = mp_get_song_uid(song_id);
    auto it = std::find_if(mp_ctx.pins.begin(), mp_ctx.pins.end(),
        [&uid](Pin& pin) {
            return test_pin_with_uid(&pin, uid);
        });

    if (it != mp_ctx.pins.end())
    {
        ctx.conn.delete_pin(uid);
        mp_ctx.pins.erase(it);
        return false;
    }

    PinID pin_id = ctx.conn.create_pin(uid);
    mp_ctx.pins.push_front(Pin{
                .song = mp_get_song(song_id),
                .album = nullptr,
                .artist = nullptr,
                .playlist = nullptr,
                .uid = uid,
                .type = MPUID_SONG,
                .last_accessed = pin_id.last_accessed
            });
    return true;
}

bool mp_toggle_pin_album(AlbumID album_id)
{
    MPUID uid = mp_get_album_uid(album_id);
    auto it = std::find_if(mp_ctx.pins.begin(), mp_ctx.pins.end(),
        [&uid](Pin& pin) {
            return test_pin_with_uid(&pin, uid);
        });

    if (it != mp_ctx.pins.end())
    {
        ctx.conn.delete_pin(uid);
        mp_ctx.pins.erase(it);
        return false;
    }

    PinID pin_id = ctx.conn.create_pin(uid);
    mp_ctx.pins.push_front(Pin{
                .song = nullptr,
                .album = mp_get_album(album_id),
                .artist = nullptr,
                .playlist = nullptr,
                .uid = uid,
                .type = MPUID_ALBUM,
                .last_accessed = pin_id.last_accessed
            });
    return true;
}

bool mp_toggle_pin_artist(ArtistID artist_id)
{
    MPUID uid = mp_get_artist_uid(artist_id);
    auto it = std::find_if(mp_ctx.pins.begin(), mp_ctx.pins.end(),
        [&uid](Pin& pin) {
            return test_pin_with_uid(&pin, uid);
        });

    if (it != mp_ctx.pins.end())
    {
        ctx.conn.delete_pin(uid);
        mp_ctx.pins.erase(it);
        return false;
    }

    PinID pin_id = ctx.conn.create_pin(uid);
    mp_ctx.pins.push_front(Pin{
                .song = nullptr,
                .album = nullptr,
                .artist = mp_get_artist(artist_id),
                .playlist = nullptr,
                .uid = uid,
                .type = MPUID_ARTIST,
                .last_accessed = pin_id.last_accessed
            });
    return true;
}

bool mp_toggle_pin_playlist(PlaylistID playlist_id)
{
    MPUID uid = mp_get_playlist_uid(playlist_id);
    auto it = std::find_if(mp_ctx.pins.begin(), mp_ctx.pins.end(),
        [&uid](Pin& pin) {
            return test_pin_with_uid(&pin, uid);
        });

    if (it != mp_ctx.pins.end())
    {
        ctx.conn.delete_pin(uid);
        mp_ctx.pins.erase(it);
        return false;
    }

    PinID pin_id = ctx.conn.create_pin(uid);
    mp_ctx.pins.push_front(Pin{
                .song = nullptr,
                .album = nullptr,
                .artist = nullptr,
                .playlist = mp_get_playlist(playlist_id),
                .uid = uid,
                .type = MPUID_PLAYLIST,
                .last_accessed = pin_id.last_accessed
            });
    return true;
}

SearchResult<SongAlbumArtist> mp_search_songs(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<SongID> search_result_id = ctx.conn.search_songs(search_query, results_per_page, page_num);
    SearchResult<SongAlbumArtist> search_result{};
    search_result.page_num = search_result_id.page_num;
    search_result.num_pages = search_result_id.num_pages;
    search_result.results_per_page = search_result_id.results_per_page;
    search_result.num_results = search_result_id.num_results;
    for (SongID song_id : search_result_id.entries)
    {
        SongAlbumArtist song{};
        song.song = mp_get_song(song_id);
        song.album = mp_get_album_from_song(song_id);
        song.artist = mp_get_artist_from_song(song_id);
        assert(song.song != nullptr);
        search_result.entries.push_back(std::move(song));
    }
    return search_result;
}

SearchResult<LruCacheRef<Album>> mp_search_albums(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<AlbumID> search_result_id = ctx.conn.search_albums(search_query, results_per_page, page_num);
    SearchResult<LruCacheRef<Album>> search_result{};
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

SearchResult<LruCacheRef<Artist>> mp_search_artists(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<ArtistID> search_result_id = ctx.conn.search_artists(search_query, results_per_page, page_num);
    SearchResult<LruCacheRef<Artist>> search_result{};
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

SearchResult<LruCacheRef<Playlist>> mp_search_playlists(const char* search_query, int results_per_page, int page_num)
{
    SearchResultID<PlaylistID> search_result_id = ctx.conn.search_playlists(search_query, results_per_page, page_num);
    SearchResult<LruCacheRef<Playlist>> search_result{};
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

SearchResult<SongAlbum> mp_get_paginated_songs_from_artist(ArtistID artist_id, int results_per_page, int page_num)
{
    SearchResultID<SongID> search_result_id = ctx.conn.search_songs_from_artist(artist_id, results_per_page, page_num);
    SearchResult<SongAlbum> search_result{};
    search_result.page_num = search_result_id.page_num;
    search_result.num_pages = search_result_id.num_pages;
    search_result.results_per_page = search_result_id.results_per_page;
    search_result.num_results = search_result_id.num_results;
    for (SongID song_id : search_result_id.entries)
    {
        SongAlbum song{};
        song.song = mp_get_song(song_id);
        song.album = mp_get_album_from_song(song_id);
        assert(song.song != nullptr);
        search_result.entries.push_back(std::move(song));
    }
    return search_result;
}

SearchResult<LruCacheRef<Album>> mp_get_paginated_albums_from_artist(ArtistID artist_id, int results_per_page, int page_num)
{
    SearchResultID<AlbumID> search_result_id = ctx.conn.search_albums_from_artist(artist_id, results_per_page, page_num);
    SearchResult<LruCacheRef<Album>> search_result{};
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

LruCacheRef<Song> mp_get_song(SongID song_id)
{
    if (song_id == INVALID_ID)
        return nullptr;
    LruCacheRef<Song> song = mp_ctx.songs.get(song_id);
    if (song == nullptr)
        song = mp_ctx.songs.put(song_id, ctx.conn.get_song_info(song_id));
    return song;
}

LruCacheRef<Album> mp_get_album(AlbumID album_id)
{
    if (album_id == INVALID_ID)
        return nullptr;
    LruCacheRef<Album> album = mp_ctx.albums.get(album_id);
    if (album == nullptr)
        album = mp_ctx.albums.put(album_id, ctx.conn.get_album_info(album_id));
    return album;
}

LruCacheRef<Artist> mp_get_artist(ArtistID artist_id)
{
    if (artist_id == INVALID_ID)
        return nullptr;
    LruCacheRef<Artist> artist = mp_ctx.artists.get(artist_id);
    if (artist == nullptr)
        artist = mp_ctx.artists.put(artist_id, ctx.conn.get_artist_info(artist_id));
    return artist;
}

LruCacheRef<Playlist> mp_get_playlist(PlaylistID playlist_id)
{
    LruCacheRef<Playlist> playlist = mp_ctx.playlists.get(playlist_id);
    if (playlist == nullptr)
        playlist = mp_ctx.playlists.put(playlist_id, ctx.conn.get_playlist_info(playlist_id));
    return playlist;
}

LruCacheRef<Artist> mp_get_artist_from_song(SongID song_id)
{
    LruCacheRef<ArtistID> artist_id = mp_ctx.song_artist.get(song_id);
    if (artist_id == nullptr)
        artist_id = mp_ctx.song_artist.put(song_id, ctx.conn.get_artist_from_song(song_id));
    return mp_get_artist(*artist_id);
}

LruCacheRef<Album> mp_get_album_from_song(SongID song_id)
{
    LruCacheRef<AlbumID> album_id = mp_ctx.song_album.get(song_id);
    if (album_id == nullptr)
        album_id = mp_ctx.song_album.put(song_id, ctx.conn.get_album_from_song(song_id));
    return mp_get_album(*album_id);
}

LruCacheRef<Artist> mp_get_artist_from_album(AlbumID album_id)
{
    LruCacheRef<ArtistID> artist_id = mp_ctx.album_artist.get(album_id);
    if (artist_id == nullptr)
        artist_id = mp_ctx.album_artist.put(album_id, ctx.conn.get_artist_from_album(album_id));
    return mp_get_artist(*artist_id);
}

int mp_get_song_track(SongID song_id)
{
    return ctx.conn.get_song_track(song_id);
}

std::vector<SongTrackID> mp_get_songs_from_album(AlbumID album_id)
{
    return ctx.conn.get_songs_from_album(album_id);
}

std::vector<SongTrackID> mp_get_songs_from_playlist(PlaylistID playlist_id)
{
    return ctx.conn.get_songs_from_playlist(playlist_id);
}

std::vector<SongID> mp_get_songs_from_artist(ArtistID artist_id)
{
    return ctx.conn.get_songs_from_artist(artist_id);
}

std::vector<AlbumID> mp_get_albums_from_artist(ArtistID artist_id)
{
    return ctx.conn.get_albums_from_artist(artist_id);
}

std::vector<LruCacheRef<Playlist>> mp_get_playlists()
{
    std::vector<PlaylistID> playlist_ids = ctx.conn.get_playlists();
    std::vector<LruCacheRef<Playlist>> playlists{};
    for (PlaylistID playlist_id : playlist_ids)
        playlists.emplace_back(mp_get_playlist(playlist_id));
    return playlists;
}

void mp_play_previous()
{
    if (mp_ctx.song_history.size() == 0)
        return;

    if (mp_ctx.current_song.song != nullptr)
    {
        SongID song_id = mp_ctx.current_song.song->id;
        mp_ctx.queue.emplace_front(
                    std::move(mp_ctx.current_song.song),
                    mp_get_album_from_song(song_id),
                    mp_get_artist_from_song(song_id)
                );
    }

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
    if (mp_ctx.current_song.song != nullptr)
        mp_ctx.song_history.push_back(mp_ctx.current_song.song->id);

    if (mp_ctx.loop_mode == LOOP_TRACK && mp_ctx.current_song.song != nullptr) {
        mp_play_song(mp_ctx.current_song.song->id);
        return;
    }
    mp_ctx.paused = false;
    if (mp_ctx.queue.size() == 0) {
        if (mp_ctx.playing_group) {
            play_next_group_song();
        } else if (mp_ctx.autoplay) {
            assert(mp_ctx.autoplay_queue.size() > 0);
            SongAlbumArtist song = std::move(mp_ctx.autoplay_queue.front());
            mp_ctx.autoplay_queue.pop_front();
            mp_play_song(song.song->id);

            SongAlbumArtist new_song{};
            new_song.song = mp_get_song(ctx.conn.get_random_song());
            new_song.album = mp_get_album_from_song(new_song.song->id);
            new_song.artist = mp_get_artist_from_song(new_song.song->id);
            mp_ctx.autoplay_queue.push_back(std::move(new_song));

        } else if (ctx.current_song_loaded) {
            mp_ctx.current_song.song = nullptr;
            mp_ctx.current_song.artist = nullptr;
            mp_ctx.current_song.album = nullptr;
            mp_ctx.current_song_lyrics[0] = '\0';
            ma_sound_uninit(&ctx.current_song_sound);
            ctx.current_song_loaded = false;
        }
    } else {
        SongAlbumArtist song = std::move(mp_ctx.queue.front());
        mp_ctx.queue.pop_front();
        mp_play_song(song.song->id);
    }
}

Art mp_get_song_art(SongID song_id)
{
    return ctx.conn.get_song_art(song_id);
}

Art mp_get_album_art(AlbumID album_id)
{
    return ctx.conn.get_album_art(album_id);
}

Art mp_get_artist_art(ArtistID artist_id)
{
    return ctx.conn.get_artist_art(artist_id);
}

Art mp_get_playlist_art(PlaylistID playlist_id)
{
    return ctx.conn.get_playlist_art(playlist_id);
}

Art mp_get_art(MPUID uid)
{
    switch (db_get_uid_type(uid))
    {
        case MPUID_SONG:
            return ctx.conn.get_song_art(db_get_song_uid(uid));
        case MPUID_ALBUM:
            return ctx.conn.get_album_art(db_get_album_uid(uid));
        case MPUID_ARTIST:
            return ctx.conn.get_artist_art(db_get_artist_uid(uid));
        case MPUID_PLAYLIST:
            return ctx.conn.get_playlist_art(db_get_playlist_uid(uid));
        default:
            return {};
    }
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
    (void)db_path;
    return;
    //if (ctx.current_song_loaded)
    //    ma_sound_uninit(&ctx.current_song_sound);
    //ctx.current_song_loaded = false;

    //mp_ctx.current_song.release();
    //mp_ctx.current_song_artist.release();
    //mp_ctx.current_song_album.release();
    //mp_ctx.queue.clear();
    //mp_ctx.autoplay_queue.clear();
    //mp_ctx.group_queue.clear();
    //mp_ctx.song_history.clear();
    //mp_ctx.pins.clear();
    //mp_ctx.added_songs.clear();
    //mp_ctx.added_artists.clear();
    //mp_ctx.added_albums.clear();
    //mp_ctx.playing_group = false;
    //mp_ctx.autoplay = false;

    //mp_ctx.songs.clear();
    //mp_ctx.albums.clear();
    //mp_ctx.artists.clear();
    //mp_ctx.playlists.clear();
    //mp_ctx.song_artist.clear();
    //mp_ctx.song_album.clear();
    //mp_ctx.album_artist.clear();
}

void mp_init()
{
    ctx.mt.seed(std::chrono::steady_clock::now().time_since_epoch().count());

    mp_ctx.volume = 1.0f;
    mp_ctx.shuffle = true;
    ma_result res;
    res = ma_engine_init(NULL, &ctx.engine);
    if (res != MA_SUCCESS)
        exit(1);

    std::vector<PinID> pin_ids = ctx.conn.get_pins();
    mp_ctx.pins.resize(pin_ids.size());
    std::transform(pin_ids.begin(), pin_ids.end(), mp_ctx.pins.begin(),
        [](PinID& pin_id) -> Pin
        {
            Pin pin{};
            int id = mp_get_id(pin_id.uid);
            pin.uid = pin_id.uid;
            pin.type = mp_get_uid_type(pin_id.uid);
            pin.last_accessed = pin_id.last_accessed;
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

    mp_ctx.num_songs = ctx.conn.get_num_songs();
    mp_ctx.num_albums = ctx.conn.get_num_albums();
    mp_ctx.num_artists = ctx.conn.get_num_artists();
    mp_ctx.num_playlists = ctx.conn.get_num_playlists();
}

void mp_cleanup()
{
    if (ctx.current_song_loaded)
        ma_sound_uninit(&ctx.current_song_sound);
    ma_engine_uninit(&ctx.engine);
}

void mp_update()
{
    if (mp_ctx.current_song.song != nullptr) {
        ma_sound_get_cursor_in_seconds(&ctx.current_song_sound, &mp_ctx.current_song_cursor);
    }
    if (ctx.song_ended) {
        mp_play_next();
        ctx.song_ended = false;
    }
}

