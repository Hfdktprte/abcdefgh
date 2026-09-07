/**
 * EOS M multi-build selector.
 *
 * Canon only executes /autoexec.bin.  Complete builds therefore live in
 * /BUILDS/<name>/ and are exchanged with the root build on the next boot.
 * The exchange happens before ML opens configuration, fonts or modules.
 */

#include "dryos.h"
#include "fio-ml.h"
#include "gui.h"
#include "menu.h"
#include "notify_box.h"
#include "property.h"
#include "tasks.h"
#include "timer.h"
#include "build-selector.h"

#if defined(CONFIG_EOSM) && defined(CONFIG_SLIM_MENUS)

#define BUILDSEL_DIR            "BUILDS"
#define BUILDSEL_CURRENT_FILE   "BUILDS/CURRENT.TXT"
#define BUILDSEL_PENDING_FILE   "BUILDS/NEXT.TXT"
#define BUILDSEL_JOURNAL_FILE   "BUILDS/SWAP.TXN"
#define BUILDSEL_ERROR_FILE     "BUILDS/SWAP.ERR"
#define BUILDSEL_MAX_SLOTS      8
#define BUILDSEL_NAME_MAX       10
#define BUILDSEL_BOOT_WINDOW_MS 12000

static char slot_names[BUILDSEL_MAX_SLOTS][BUILDSEL_NAME_MAX + 1];
static struct menu_entry slot_entries[BUILDSEL_MAX_SLOTS];
static int slot_count;
static int selector_initialized;
static int selector_boot_deadline;
static int selector_boot_up_held;

static int buildsel_valid_name(const char *name)
{
    int len = 0;
    if (!name || !name[0] || name[0] == '.') return 0;
    while (name[len])
    {
        char c = name[len];
        if (len >= BUILDSEL_NAME_MAX) return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' '))
            return 0;
        len++;
    }
    return len > 0;
}

static int buildsel_read_name(const char *path, char *name)
{
    FILE *f = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    if (!f) return 0;
    int n = FIO_ReadFile(f, name, BUILDSEL_NAME_MAX);
    FIO_CloseFile(f);
    if (n <= 0) return 0;
    name[n] = 0;
    while (n > 0 && (name[n-1] == '\r' || name[n-1] == '\n' || name[n-1] == ' '))
        name[--n] = 0;
    return buildsel_valid_name(name);
}

static int buildsel_write_name(const char *path, const char *name)
{
    FILE *f = FIO_CreateFile(path);
    if (!f) return 0;
    int len = strlen(name);
    int ok = FIO_WriteFile(f, name, len) == len;
    FIO_CloseFile(f);
    return ok;
}

static void buildsel_write_error(const char *message)
{
    FILE *f = FIO_CreateFile(BUILDSEL_ERROR_FILE);
    if (!f) return;
    FIO_WriteFile(f, message, strlen(message));
    FIO_CloseFile(f);
}

static void buildsel_slot_path(char *path, int size, const char *name,
                               const char *leaf)
{
    snprintf(path, size, "%s/%s/%s", BUILDSEL_DIR, name, leaf);
}

static int buildsel_slot_is_compatible(const char *name)
{
    char path[FIO_MAX_PATH_LENGTH];
    buildsel_slot_path(path, sizeof(path), name, "autoexec.bin");
    if (!is_file(path)) return 0;
    buildsel_slot_path(path, sizeof(path), name, "ML");
    if (!is_dir(path)) return 0;
    buildsel_slot_path(path, sizeof(path), name, "ML/BUILDSEL.OK");
    return is_file(path);
}

static int buildsel_prepare_current(char *current)
{
    if (!is_dir(BUILDSEL_DIR) && FIO_CreateDirectory(BUILDSEL_DIR))
        return 0;

    if (!buildsel_read_name(BUILDSEL_CURRENT_FILE, current))
    {
        snprintf(current, BUILDSEL_NAME_MAX + 1, "SLIMGUI");
        char path[FIO_MAX_PATH_LENGTH];
        buildsel_slot_path(path, sizeof(path), current, "");
        if (is_dir(path))
        {
            /* Never overwrite a user's prepared slot. */
            snprintf(current, BUILDSEL_NAME_MAX + 1, "CURRENT");
            buildsel_slot_path(path, sizeof(path), current, "");
            if (is_dir(path)) return 0;
        }
        if (FIO_CreateDirectory(path)) return 0;
        if (!buildsel_write_name(BUILDSEL_CURRENT_FILE, current)) return 0;
    }
    return 1;
}

static int buildsel_rename(const char *from, const char *to)
{
    if (FIO_RenameFile(from, to) == 0) return 1;
    return 0;
}

void build_selector_early_apply(void)
{
    char target[BUILDSEL_NAME_MAX + 1];
    char current[BUILDSEL_NAME_MAX + 1];
    char current_ml[FIO_MAX_PATH_LENGTH];
    char target_ml[FIO_MAX_PATH_LENGTH];
    char current_bin[FIO_MAX_PATH_LENGTH];
    char target_bin[FIO_MAX_PATH_LENGTH];

    if (!is_file(BUILDSEL_PENDING_FILE)) return;
    if (!buildsel_read_name(BUILDSEL_PENDING_FILE, target) ||
        !buildsel_prepare_current(current) ||
        !buildsel_slot_is_compatible(target))
    {
        buildsel_write_error("Invalid or incompatible selected build\n");
        FIO_RemoveFile(BUILDSEL_PENDING_FILE);
        return;
    }
    if (!strcmp(target, current))
    {
        FIO_RemoveFile(BUILDSEL_PENDING_FILE);
        return;
    }

    buildsel_slot_path(current_ml, sizeof(current_ml), current, "ML");
    buildsel_slot_path(target_ml, sizeof(target_ml), target, "ML");
    buildsel_slot_path(current_bin, sizeof(current_bin), current, "autoexec.bin");
    buildsel_slot_path(target_bin, sizeof(target_bin), target, "autoexec.bin");

    /* The active slot must be empty. Refuse instead of overwriting files. */
    if (is_dir(current_ml) || is_file(current_bin))
    {
        buildsel_write_error("Current build slot is not empty\n");
        FIO_RemoveFile(BUILDSEL_PENDING_FILE);
        return;
    }

    buildsel_write_name(BUILDSEL_JOURNAL_FILE, target);

    /* Move the large directory first; no ML files have been opened yet. */
    if (!buildsel_rename("ML", current_ml)) goto failed;
    if (!buildsel_rename(target_ml, "ML"))
    {
        buildsel_rename(current_ml, "ML");
        goto failed;
    }

    /* autoexec is last, keeping the no-autoexec interval extremely short. */
    if (!buildsel_rename("autoexec.bin", current_bin))
    {
        buildsel_rename("ML", target_ml);
        buildsel_rename(current_ml, "ML");
        goto failed;
    }
    if (!buildsel_rename(target_bin, "autoexec.bin"))
    {
        buildsel_rename(current_bin, "autoexec.bin");
        buildsel_rename("ML", target_ml);
        buildsel_rename(current_ml, "ML");
        goto failed;
    }

    buildsel_write_name(BUILDSEL_CURRENT_FILE, target);
    FIO_RemoveFile(BUILDSEL_PENDING_FILE);
    FIO_RemoveFile(BUILDSEL_JOURNAL_FILE);

    /* Start the newly selected root build without letting this build continue
     * with a different ML directory. */
    int reboot = 0;
    prop_request_change(PROP_REBOOT, &reboot, 4);
    while (1) msleep(1000);

failed:
    buildsel_write_error("Build swap failed; original build restored\n");
    FIO_RemoveFile(BUILDSEL_PENDING_FILE);
    FIO_RemoveFile(BUILDSEL_JOURNAL_FILE);
}

static void buildsel_sort_slots(void)
{
    for (int i = 0; i < slot_count; i++)
        for (int j = i + 1; j < slot_count; j++)
            if (strcmp(slot_names[i], slot_names[j]) > 0)
            {
                char tmp[BUILDSEL_NAME_MAX + 1];
                snprintf(tmp, sizeof(tmp), "%s", slot_names[i]);
                snprintf(slot_names[i], sizeof(slot_names[i]), "%s", slot_names[j]);
                snprintf(slot_names[j], sizeof(slot_names[j]), "%s", tmp);
            }
}

static void buildsel_scan(void)
{
    slot_count = 0;
    char current[BUILDSEL_NAME_MAX + 1] = "";
    buildsel_prepare_current(current);

    struct fio_file file;
    struct fio_dirent *dir = FIO_FindFirstEx(BUILDSEL_DIR, &file);
    if (IS_ERROR(dir)) return;
    do
    {
        if (!(file.mode & ATTR_DIRECTORY)) continue;
        if (!buildsel_valid_name(file.name)) continue;
        if (!strcmp(file.name, current)) continue;
        if (!buildsel_slot_is_compatible(file.name)) continue;
        snprintf(slot_names[slot_count], sizeof(slot_names[slot_count]), "%s", file.name);
        if (++slot_count >= BUILDSEL_MAX_SLOTS) break;
    } while (FIO_FindNextEx(dir, &file) == 0);
    FIO_FindClose(dir);
    buildsel_sort_slots();
}

static MENU_SELECT_FUNC(buildsel_select)
{
    int index = (int)(intptr_t)priv;
    if (index < 0 || index >= slot_count) return;
    if (!buildsel_write_name(BUILDSEL_PENDING_FILE, slot_names[index]))
    {
        NotifyBox(3000, "Could not save build selection");
        return;
    }
    NotifyBox(10000, "%s selected\nPlease Restart", slot_names[index]);
}

static MENU_UPDATE_FUNC(buildsel_update)
{
    int index = (int)(intptr_t)entry->priv;
    if (index >= 0 && index < slot_count)
    {
        MENU_SET_NAME("%s", slot_names[index]);
        MENU_SET_VALUE("Select");
        MENU_SET_HELP("Switch to this build on the next restart.");
    }
}

static void buildsel_refresh_entries(void)
{
    buildsel_scan();
    for (int i = 0; i < BUILDSEL_MAX_SLOTS; i++)
    {
        slot_entries[i].shidden = i >= slot_count;
        slot_entries[i].name = i < slot_count ? slot_names[i] : "(empty)";
        slot_entries[i].priv = (void *)(intptr_t)i;
        slot_entries[i].select = buildsel_select;
        slot_entries[i].update = buildsel_update;
        slot_entries[i].icon_type = IT_ACTION;
    }
}

void gui_open_build_selector(void)
{
    buildsel_refresh_entries();
    if (!slot_count)
    {
        NotifyBox(5000, "No compatible builds in /BUILDS");
        return;
    }
    gui_open_menu_at_entry("Builds", slot_names[0]);
}

int build_selector_note_boot_up(int event_code)
{
    if (event_code == BGMT_PRESS_UP)
    {
        selector_boot_up_held = 1;
        return 1;
    }
    if (event_code == BGMT_UNPRESS_UP)
    {
        selector_boot_up_held = 0;
        return 1;
    }
    return 0;
}

static void buildsel_open_held_at_boot(int timer, void *opaque)
{
    (void)timer;
    (void)opaque;
    extern int ml_started;
    if (!selector_boot_up_held || !build_selector_boot_window_active()) return;
    if (!ml_started)
    {
        delayed_call(50, buildsel_open_held_at_boot, 0);
        return;
    }
    gui_open_build_selector();
}

int build_selector_boot_window_active(void)
{
    return selector_initialized && get_ms_clock() < selector_boot_deadline;
}

static void build_selector_init(void)
{
    char current[BUILDSEL_NAME_MAX + 1];
    buildsel_prepare_current(current);
    buildsel_refresh_entries();
    menu_add("Builds", slot_entries, COUNT(slot_entries));
    selector_boot_deadline = get_ms_clock() + BUILDSEL_BOOT_WINDOW_MS;
    selector_initialized = 1;
    if (selector_boot_up_held)
        delayed_call(550, buildsel_open_held_at_boot, 0);
}
INIT_FUNC("build-selector", build_selector_init);

#else

void build_selector_early_apply(void) {}
int build_selector_boot_window_active(void) { return 0; }
int build_selector_note_boot_up(int event_code) { (void)event_code; return 0; }
void gui_open_build_selector(void) {}

#endif
