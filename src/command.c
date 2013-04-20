/**
 * vimb - a webkit based vim like browser.
 *
 * Copyright (C) 2012-2013 Daniel Carl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 */

#include "main.h"
#include "command.h"
#include "keybind.h"
#include "setting.h"
#include "completion.h"
#include "hints.h"
#include "util.h"
#include "searchengine.h"
#include "history.h"
#include "bookmark.h"
#include "dom.h"

typedef struct {
    char    *file;
    Element *element;
} OpenEditorData;

static void editor_resume(GPid pid, int status, OpenEditorData *data);

extern VbCore vb;
extern const unsigned int INPUT_LENGTH;

static GHashTable *commands;
static CommandInfo cmd_list[] = {
    /* command               function                      mode */
    {"open",                 command_open,                 {VB_TARGET_CURRENT, ""}},
    {"tabopen",              command_open,                 {VB_TARGET_NEW, ""}},
    {"open-closed",          command_open_closed,          {VB_TARGET_CURRENT}},
    {"tabopen-closed",       command_open_closed,          {VB_TARGET_NEW}},
    {"input",                command_input,                {0, ":"}},
    {"inputuri",             command_input,                {VB_INPUT_CURRENT_URI, ":"}},
    {"quit",                 command_close,                {0}},
    {"source",               command_view_source,          {0}},
    {"back",                 command_navigate,             {VB_NAVIG_BACK}},
    {"forward",              command_navigate,             {VB_NAVIG_FORWARD}},
    {"reload",               command_navigate,             {VB_NAVIG_RELOAD}},
    {"reload!",              command_navigate,             {VB_NAVIG_RELOAD_FORCE}},
    {"stop",                 command_navigate,             {VB_NAVIG_STOP_LOADING}},
    {"jumpleft",             command_scroll,               {VB_SCROLL_TYPE_JUMP | VB_SCROLL_DIRECTION_LEFT}},
    {"jumpright",            command_scroll,               {VB_SCROLL_TYPE_JUMP | VB_SCROLL_DIRECTION_RIGHT}},
    {"jumptop",              command_scroll,               {VB_SCROLL_TYPE_JUMP | VB_SCROLL_DIRECTION_TOP}},
    {"jumpbottom",           command_scroll,               {VB_SCROLL_TYPE_JUMP | VB_SCROLL_DIRECTION_DOWN}},
    {"pageup",               command_scroll,               {VB_SCROLL_TYPE_SCROLL | VB_SCROLL_DIRECTION_TOP | VB_SCROLL_UNIT_PAGE}},
    {"pagedown",             command_scroll,               {VB_SCROLL_TYPE_SCROLL | VB_SCROLL_DIRECTION_DOWN | VB_SCROLL_UNIT_PAGE}},
    {"halfpageup",           command_scroll,               {VB_SCROLL_TYPE_SCROLL | VB_SCROLL_DIRECTION_TOP | VB_SCROLL_UNIT_HALFPAGE}},
    {"halfpagedown",         command_scroll,               {VB_SCROLL_TYPE_SCROLL | VB_SCROLL_DIRECTION_DOWN | VB_SCROLL_UNIT_HALFPAGE}},
    {"scrollleft",           command_scroll,               {VB_SCROLL_TYPE_SCROLL | VB_SCROLL_DIRECTION_LEFT | VB_SCROLL_UNIT_LINE}},
    {"scrollright",          command_scroll,               {VB_SCROLL_TYPE_SCROLL | VB_SCROLL_DIRECTION_RIGHT | VB_SCROLL_UNIT_LINE}},
    {"scrollup",             command_scroll,               {VB_SCROLL_TYPE_SCROLL | VB_SCROLL_DIRECTION_TOP | VB_SCROLL_UNIT_LINE}},
    {"scrolldown",           command_scroll,               {VB_SCROLL_TYPE_SCROLL | VB_SCROLL_DIRECTION_DOWN | VB_SCROLL_UNIT_LINE}},
    {"nmap",                 command_map,                  {VB_MODE_NORMAL}},
    {"imap",                 command_map,                  {VB_MODE_INSERT}},
    {"cmap",                 command_map,                  {VB_MODE_COMMAND}},
    {"hmap",                 command_map,                  {VB_MODE_HINTING}},
    {"smap",                 command_map,                  {VB_MODE_SEARCH}},
    {"nunmap",               command_unmap,                {VB_MODE_NORMAL}},
    {"iunmap",               command_unmap,                {VB_MODE_INSERT}},
    {"cunmap",               command_unmap,                {VB_MODE_COMMAND}},
    {"hunmap",               command_unmap,                {VB_MODE_HINTING}},
    {"sunmap",               command_map,                  {VB_MODE_SEARCH}},
    {"set",                  command_set,                  {0}},
    {"complete",             command_complete,             {0}},
    {"complete-back",        command_complete,             {1}},
    {"inspect",              command_inspect,              {0}},
    {"hint-link",            command_hints,                {HINTS_TYPE_LINK | HINTS_PROCESS_OPEN, "."}},
    {"hint-link-new",        command_hints,                {HINTS_TYPE_LINK | HINTS_PROCESS_OPEN | HINTS_OPEN_NEW, ","}},
    {"hint-input-open",      command_hints,                {HINTS_TYPE_LINK | HINTS_PROCESS_INPUT, ";o"}},
    {"hint-input-tabopen",   command_hints,                {HINTS_TYPE_LINK | HINTS_PROCESS_INPUT | HINTS_OPEN_NEW, ";t"}},
    {"hint-yank",            command_hints,                {HINTS_TYPE_LINK | HINTS_PROCESS_YANK, ";y"}},
    {"hint-image-open",      command_hints,                {HINTS_TYPE_IMAGE | HINTS_PROCESS_OPEN, ";i"}},
    {"hint-image-tabopen",   command_hints,                {HINTS_TYPE_IMAGE | HINTS_PROCESS_OPEN | HINTS_OPEN_NEW, ";I"}},
    {"hint-editor",          command_hints,                {HINTS_TYPE_EDITABLE, ";e"}},
    {"hint-focus-next",      command_hints_focus,          {0}},
    {"hint-focus-prev",      command_hints_focus,          {1}},
    {"yank-uri",             command_yank,                 {COMMAND_YANK_PRIMARY | COMMAND_YANK_SECONDARY | COMMAND_YANK_URI}},
    {"yank-selection",       command_yank,                 {COMMAND_YANK_PRIMARY | COMMAND_YANK_SECONDARY | COMMAND_YANK_SELECTION}},
    {"open-clipboard",       command_paste,                {VB_CLIPBOARD_PRIMARY | VB_CLIPBOARD_SECONDARY | VB_TARGET_CURRENT}},
    {"tabopen-clipboard",    command_paste,                {VB_CLIPBOARD_PRIMARY | VB_CLIPBOARD_SECONDARY | VB_TARGET_NEW}},
    {"search-forward",       command_search,               {VB_SEARCH_FORWARD}},
    {"search-backward",      command_search,               {VB_SEARCH_BACKWARD}},
    {"searchengine-add",     command_searchengine,         {1}},
    {"searchengine-remove",  command_searchengine,         {0}},
    {"searchengine-default", command_searchengine_default, {0}},
    {"zoomin",               command_zoom,                 {COMMAND_ZOOM_IN}},
    {"zoomout",              command_zoom,                 {COMMAND_ZOOM_OUT}},
    {"zoominfull",           command_zoom,                 {COMMAND_ZOOM_IN | COMMAND_ZOOM_FULL}},
    {"zoomoutfull",          command_zoom,                 {COMMAND_ZOOM_OUT | COMMAND_ZOOM_FULL}},
    {"zoomreset",            command_zoom,                 {COMMAND_ZOOM_RESET}},
    {"hist-next",            command_history,              {0}},
    {"hist-prev",            command_history,              {1}},
    {"run",                  command_run_multi,            {0}},
    {"bookmark-add",         command_bookmark,             {1}},
    {"eval",                 command_eval,                 {0}},
    {"editor",               command_editor,               {0}},
};


void command_init(void)
{
    guint i;
    commands = g_hash_table_new(g_str_hash, g_str_equal);

    for (i = 0; i < LENGTH(cmd_list); i++) {
        g_hash_table_insert(commands, (gpointer)cmd_list[i].name, &cmd_list[i]);
    }
}

GList *command_get_all(void)
{
    return g_hash_table_get_keys(commands);
}

void command_cleanup(void)
{
    if (commands) {
        g_hash_table_destroy(commands);
    }
}

gboolean command_exists(const char *name)
{
    return g_hash_table_contains(commands, name);
}

gboolean command_run(const char *name, const char *param)
{
    CommandInfo *command = NULL;
    gboolean result;
    Arg a;
    command = g_hash_table_lookup(commands, name);
    if (!command) {
        vb_echo(VB_MSG_ERROR, true, "Command '%s' not found", name);
        vb_set_mode(VB_MODE_NORMAL, false);

        return false;
    }
    a.i = command->arg.i;
    a.s = g_strdup(param ? param : command->arg.s);
    result = command->function(&a);
    g_free(a.s);

    return result;
}

/**
 * Runs a single command form string containing the command an possible
 * parameters.
 */
gboolean command_run_string(const char *input)
{
    gboolean success;
    char *command = NULL, *str, **token;

    if (!input || *input == '\0') {
        return false;
    }

    str =g_strdup(input);
    /* remove leading whitespace */
    g_strchug(str);

    /* get a possible command count */
    vb.state.count = g_ascii_strtoll(str, &command, 10);

    /* split the input string into command and parameter part */
    token = g_strsplit(command, " ", 2);
    g_free(str);

    if (!token[0]) {
        g_strfreev(token);
        return false;
    }
    success = command_run(token[0], token[1] ? token[1] : NULL);
    g_strfreev(token);

    return success;
}

/**
 * Runs multiple commands that are seperated by |.
 */
gboolean command_run_multi(const Arg *arg)
{
    gboolean result = true;
    char **commands;
    unsigned int len, i;

    if (!arg->s || *(arg->s) == '\0') {
        return false;
    }

    /* splits the commands */
    commands = g_strsplit(arg->s, "|", 0);

    len = g_strv_length(commands);
    if (!len) {
        g_strfreev(commands);
        return false;
    }

    for (i = 0; i < len; i++) {
        /* run the single commands */
        result = (result && command_run_string(commands[i]));
    }
    g_strfreev(commands);

    return result;
}

gboolean command_open(const Arg *arg)
{
    return vb_load_uri(arg);
}

/**
 * Reopens the last closed page.
 */
gboolean command_open_closed(const Arg *arg)
{
    gboolean result;

    Arg a = {arg->i};
    a.s = util_get_file_contents(vb.files[FILES_CLOSED], NULL);
    result = vb_load_uri(&a);
    g_free(a.s);

    return result;
}

gboolean command_input(const Arg *arg)
{
    const char *url;

    /* add current url if requested */
    if (VB_INPUT_CURRENT_URI == arg->i
        && (url = webkit_web_view_get_uri(vb.gui.webview))
    ) {
        /* append the current url to the input message */
        char *input = g_strconcat(arg->s, url, NULL);
        vb_echo_force(VB_MSG_NORMAL, false, "%s", input);
        g_free(input);
    } else {
        vb_echo_force(VB_MSG_NORMAL, false, "%s", arg->s);
    }

    vb_set_mode(VB_MODE_COMMAND, false);

    return true;
}

gboolean command_close(const Arg *arg)
{
    gtk_widget_destroy(GTK_WIDGET(vb.gui.window));

    return true;
}

gboolean command_view_source(const Arg *arg)
{
    gboolean mode = webkit_web_view_get_view_source_mode(vb.gui.webview);
    webkit_web_view_set_view_source_mode(vb.gui.webview, !mode);
    webkit_web_view_reload(vb.gui.webview);

    vb_set_mode(VB_MODE_NORMAL, false);

    return true;
}

gboolean command_navigate(const Arg *arg)
{
    WebKitWebView *view = vb.gui.webview;
    if (arg->i <= VB_NAVIG_FORWARD) {
        int count = vb.state.count ? vb.state.count : 1;
        webkit_web_view_go_back_or_forward(
            view, (arg->i == VB_NAVIG_BACK ? -count : count)
        );
    } else if (arg->i == VB_NAVIG_RELOAD) {
        webkit_web_view_reload(view);
    } else if (arg->i == VB_NAVIG_RELOAD_FORCE) {
        webkit_web_view_reload_bypass_cache(view);
    } else {
        webkit_web_view_stop_loading(view);
    }

    vb_set_mode(VB_MODE_NORMAL, false);

    return true;
}

gboolean command_scroll(const Arg *arg)
{
    gdouble max, new;
    int direction = (arg->i & (1 << 2)) ? 1 : -1;
    GtkAdjustment *adjust = (arg->i & VB_SCROLL_AXIS_H) ? vb.gui.adjust_h : vb.gui.adjust_v;

    max = gtk_adjustment_get_upper(adjust) - gtk_adjustment_get_page_size(adjust);
    /* type scroll */
    if (arg->i & VB_SCROLL_TYPE_SCROLL) {
        gdouble value;
        int count = vb.state.count ? vb.state.count : 1;
        if (arg->i & VB_SCROLL_UNIT_LINE) {
            value = vb.config.scrollstep;
        } else if (arg->i & VB_SCROLL_UNIT_HALFPAGE) {
            value = gtk_adjustment_get_page_size(adjust) / 2;
        } else {
            value = gtk_adjustment_get_page_size(adjust);
        }
        new = gtk_adjustment_get_value(adjust) + direction * value * count;
    } else if (vb.state.count) {
        /* jump - if count is set to count% of page */
        new = max * vb.state.count / 100;
    } else if (direction == 1) {
        /* jump to top */
        new = gtk_adjustment_get_upper(adjust);
    } else {
        /* jump to bottom */
        new = gtk_adjustment_get_lower(adjust);
    }
    gtk_adjustment_set_value(adjust, new > max ? max : new);

    vb_set_mode(VB_MODE_NORMAL, false);

    return true;
}

gboolean command_map(const Arg *arg)
{
    char *key;

    vb_set_mode(VB_MODE_NORMAL, false);

    if ((key = strchr(arg->s, '='))) {
        *key = '\0';
        if (arg->s) {
            return keybind_add_from_string(arg->s, key + 1, arg->i);
        }
    }
    return false;
}

gboolean command_unmap(const Arg *arg)
{
    vb_set_mode(VB_MODE_NORMAL, false);

    return keybind_remove_from_string(arg->s, arg->i);
}

gboolean command_set(const Arg *arg)
{
    gboolean success;
    char *param = NULL, *line = NULL;

    if (!arg->s || !strlen(arg->s)) {
        return false;
    }

    line = g_strdup(arg->s);
    g_strstrip(line);

    /* split the input string into parameter and value part */
    if ((param = strchr(line, '='))) {
        *param = '\0';
        param++;
        success = setting_run(line, param ? param : NULL);
    } else {
        success = setting_run(line, NULL);
    }
    g_free(line);

    vb_set_mode(VB_MODE_NORMAL, false);

    return success;
}

gboolean command_complete(const Arg *arg)
{
    completion_complete(arg->i ? true : false);

    vb_set_mode(VB_MODE_COMMAND | VB_MODE_COMPLETE, false);

    return true;
}

gboolean command_inspect(const Arg *arg)
{
    gboolean enabled;
    WebKitWebSettings *settings = NULL;

    vb_set_mode(VB_MODE_NORMAL, false);

    settings = webkit_web_view_get_settings(vb.gui.webview);
    g_object_get(G_OBJECT(settings), "enable-developer-extras", &enabled, NULL);
    if (enabled) {
        if (vb.state.is_inspecting) {
            webkit_web_inspector_close(vb.gui.inspector);
        } else {
            webkit_web_inspector_show(vb.gui.inspector);
        }
        return true;
    }

    vb_echo(VB_MSG_ERROR, true, "webinspector is not enabled");

    return false;
}

gboolean command_hints(const Arg *arg)
{
    vb_echo_force(VB_MSG_NORMAL, false, "%s", arg->s);
    /* mode will be set in hints_create - so we don't neet to do it here */
    hints_create(NULL, arg->i, (arg->s ? strlen(arg->s) : 0));

    return true;
}

gboolean command_hints_focus(const Arg *arg)
{
    hints_focus_next(arg->i ? true : false);

    return true;
}

gboolean command_yank(const Arg *arg)
{
    vb_set_mode(VB_MODE_NORMAL, true);

    if (arg->i & COMMAND_YANK_SELECTION) {
        char *text = NULL;
        /* copy current selection to clipboard */
        webkit_web_view_copy_clipboard(vb.gui.webview);
        text = gtk_clipboard_wait_for_text(PRIMARY_CLIPBOARD());
        if (!text) {
            text = gtk_clipboard_wait_for_text(SECONDARY_CLIPBOARD());
        }
        if (text) {
            vb_echo_force(VB_MSG_NORMAL, false, "Yanked: %s", text);
            g_free(text);

            return true;
        }

        return false;
    }
    /* use current arg.s a new clipboard content */
    Arg a = {arg->i};
    if (arg->i & COMMAND_YANK_URI) {
        /* yank current url */
        a.s = g_strdup(webkit_web_view_get_uri(vb.gui.webview));
    } else {
        a.s = arg->s ? g_strdup(arg->s) : NULL;
    }
    if (a.s) {
        vb_set_clipboard(&a);
        vb_echo_force(VB_MSG_NORMAL, false, "Yanked: %s", a.s);
        g_free(a.s);

        return true;
    }

    return false;
}

gboolean command_paste(const Arg *arg)
{
    Arg a = {.i = arg->i & VB_TARGET_NEW};
    if (arg->i & VB_CLIPBOARD_PRIMARY) {
        a.s = gtk_clipboard_wait_for_text(PRIMARY_CLIPBOARD());
    }
    if (!a.s && arg->i & VB_CLIPBOARD_SECONDARY) {
        a.s = gtk_clipboard_wait_for_text(SECONDARY_CLIPBOARD());
    }

    if (a.s) {
        vb_load_uri(&a);
        g_free(a.s);

        return true;
    }
    return false;
}

gboolean command_search(const Arg *arg)
{
    gboolean forward = !(arg->i ^ vb.state.search_dir);

    if (arg->i == VB_SEARCH_OFF && vb.state.search_query) {
        OVERWRITE_STRING(vb.state.search_query, NULL);
#ifdef FEATURE_SEARCH_HIGHLIGHT
        webkit_web_view_unmark_text_matches(vb.gui.webview);
#endif

        return true;
    }

    /* copy search query for later use */
    if (arg->s) {
        OVERWRITE_STRING(vb.state.search_query, arg->s);
        /* set dearch dir only when the searching is started */
        vb.state.search_dir = arg->i;
    }

    if (vb.state.search_query) {
#ifdef FEATURE_SEARCH_HIGHLIGHT
        webkit_web_view_mark_text_matches(vb.gui.webview, vb.state.search_query, false, 0);
        webkit_web_view_set_highlight_text_matches(vb.gui.webview, true);
#endif
        /* make sure we have a count greater than 0 */
        vb.state.count = vb.state.count ? vb.state.count : 1;
        do {
            webkit_web_view_search_text(vb.gui.webview, vb.state.search_query, false, forward, true);
        } while (--vb.state.count);
    }

    vb_set_mode(VB_MODE_SEARCH, false);

    return true;
}

gboolean command_searchengine(const Arg *arg)
{
    gboolean result;
    if (arg->i) {
        char *handle;

        if ((handle = strchr(arg->s, '='))) {
            *handle = '\0';
            handle++;
            result = searchengine_add(arg->s, handle);
        } else {
            return false;
        }
    } else {
        /* remove the search engine */
        result = searchengine_remove(arg->s);
    }

    vb_set_mode(VB_MODE_NORMAL, false);

    return result;
}

gboolean command_searchengine_default(const Arg *arg)
{
    vb_set_mode(VB_MODE_NORMAL, false);

    return searchengine_set_default(arg->s);
}

gboolean command_zoom(const Arg *arg)
{
    float step, level;
    int count;

    if (arg->i & COMMAND_ZOOM_RESET) {
        webkit_web_view_set_zoom_level(vb.gui.webview, 1.0);
        vb_set_mode(VB_MODE_NORMAL, false);

        return true;
    }

    count = vb.state.count ? vb.state.count : 1;
    level = webkit_web_view_get_zoom_level(vb.gui.webview);

    WebKitWebSettings *setting = webkit_web_view_get_settings(vb.gui.webview);
    g_object_get(G_OBJECT(setting), "zoom-step", &step, NULL);

    webkit_web_view_set_full_content_zoom(
        vb.gui.webview, (arg->i & COMMAND_ZOOM_FULL) > 0
    );

    webkit_web_view_set_zoom_level(
        vb.gui.webview,
        level + (float)(count *step) * (arg->i & COMMAND_ZOOM_IN ? 1.0 : -1.0)
    );

    vb_set_mode(VB_MODE_NORMAL, false);

    return true;

}

gboolean command_history(const Arg *arg)
{
    const char *input = GET_TEXT();
    char *entry       = history_get(input, arg->i);

    if (!entry) {
        return false;
    }

    vb_echo_force(VB_MSG_NORMAL, false, "%s", entry);
    g_free(entry);

    return true;
}

gboolean command_bookmark(const Arg *arg)
{
    bookmark_add(webkit_web_view_get_uri(vb.gui.webview), arg->s);
    vb_set_mode(VB_MODE_NORMAL, false);
    return true;
}

gboolean command_eval(const Arg *arg)
{
    gboolean success;
    char *value = NULL;

    success = vb_eval_script(
        webkit_web_view_get_main_frame(vb.gui.webview), arg->s, NULL, &value
    );
    if (success) {
        vb_echo_force(VB_MSG_NORMAL, false, "%s", value);
    } else {
        vb_echo_force(VB_MSG_ERROR, true, "%s", value);
    }
    g_free(value);
    vb_set_mode(VB_MODE_NORMAL, false);

    return success;
}

gboolean command_editor(const Arg *arg)
{
    char *file_path = NULL;
    const char *text;
    char **argv;
    int argc;
    GPid pid;
    gboolean success;

    if (!vb.config.editor_command) {
        vb_echo(VB_MSG_ERROR, true, "No editor-command configured");
        return false;
    }
    Element* active = dom_get_active_element(vb.gui.webview);

    /* check if element is suitable for editing */
    if (!active || !dom_is_editable(active)) {
        return false;
    }

    text = dom_editable_element_get_value(active);
    if (!text) {
        return false;
    }

    if (!util_create_tmp_file(text, &file_path)) {
        return false;
    }

    /* spawn editor */
    char* command = g_strdup_printf(vb.config.editor_command, file_path);
    if (!g_shell_parse_argv(command, &argc, &argv, NULL)) {
        fprintf(stderr, "Could not parse editor-command");
        g_free(command);
        return false;
    }
    g_free(command);

    success = g_spawn_async(
        NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL, &pid, NULL
    );
    g_strfreev(argv);

    if (!success) {
        unlink(file_path);
        g_free(file_path);
        return false;
    }

    /* disable the active element */
    dom_editable_element_set_disable(active, true);

    OpenEditorData *data = g_new0(OpenEditorData, 1);
    data->file    = file_path;
    data->element = active;

    g_child_watch_add(pid, (GChildWatchFunc)editor_resume, data);

    return true;
}

static void editor_resume(GPid pid, int status, OpenEditorData *data)
{
    char *text;
    if (status == 0) {
        text = util_get_file_contents(data->file, NULL);
        if (text) {
            dom_editable_element_set_value(data->element, text);
        }
        g_free(text);
    }
    dom_editable_element_set_disable(data->element, false);

    g_free(data->file);
    g_free(data);
    g_spawn_close_pid(pid);
}
