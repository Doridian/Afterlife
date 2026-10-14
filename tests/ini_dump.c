/*
 * tests/ini_dump: runs the INI-reading parts of the init sequence (no window,
 * no sound) and prints the result, then indexes the bundles the INI names.
 *
 *   ini_dump [DATA_DIR [FILE.INI]]     defaults: orig ALIFE.ini
 */
#include <stdio.h>

#include "game/config.h"
#include "game/ini.h"
#include "game/resource.h"
#include "platform/platform.h"

int main(int argc, char **argv)
{
    plat_set_data_dir(argc > 1 ? argv[1] : "orig");
    ini_set_path(argc > 2 ? argv[2] : NULL);

    AlConfig c;
    bool mismatch = false;
    cfg_set_defaults(&c);
    cfg_read_heap(&c);
    int rc_err = cfg_read_resource_paths(&c);
    cfg_read_display(&c, 1024, 768, 8, &mismatch);
    cfg_read_sound(&c);

    printf("ini=%s\n", ini_path());
    printf("heap_size=%u\n", (unsigned)c.heap_size);
    printf("cd_path=\"%s\"\nresource_file=\"%s\"\nrc_err=%d\n", c.cd_path, c.resource_file, rc_err);
    printf("resolution_max=%d req=%dx%d final=%dx%d mode=%d (desktop 1024x768)\n", c.resolution_max,
           c.req_width, c.req_height, c.width, c.height, c.display_mode);
    printf("color_depth=%d has=%d mismatch(8bpp)=%d force=%d\n", c.color_depth, c.has_color_depth,
           mismatch, c.force_display);
    printf("window_name=\"%s\"\n", c.window_name);
    printf("sound=%d music=%d,%d,%d voice=%d,%d,%d mixer=%d\n", c.sound_enabled, c.music_stream[0],
           c.music_stream[1], c.music_stream[2], c.voice_stream[0], c.voice_stream[1],
           c.voice_stream[2], c.mixer_channels);

    char buf[0x800];
    if (cfg_save_games_dir(buf))
        printf("save_games=\"%s\"\n", buf);
    if (cfg_cd_dir(buf))
        printf("cd_dir=\"%s\"\n", buf);
    cfg_savepath(buf);
    printf("savepath=\"%s\"\n", buf);
    int vflags = 0;
    printf("video_settings=%s", cfg_video_settings(&vflags) ? "" : "(absent)");
    if (cfg_video_settings(&vflags))
        printf("%d", vflags);
    printf("\n");

    bool cd_ok = rc_initialize(c.resource_file, c.cd_path);
    printf("cd_index=%u (%s) local_index=%u\n", (unsigned)rc_cd_index()->count, cd_ok ? "ok" : "FAIL",
           (unsigned)rc_local_index()->count);
    rc_shutdown();
    return rc_err || !cd_ok;
}
