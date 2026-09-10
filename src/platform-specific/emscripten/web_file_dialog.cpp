#if defined(__EMSCRIPTEN__)

#include "web_file_dialog.hpp"

#include <emscripten.h>
#include <cstdlib>
#include <cstdint>
#include <string>

// Pending dialog state. Only one open dialog is supported at a time, which
// matches how the OSD uses it (one drive click -> one picker).
static SDL_DialogFileCallback g_web_dialog_callback = nullptr;
static void                  *g_web_dialog_userdata = nullptr;
static uint32_t g_web_dialog_generation = 0;

// Called from JavaScript (via ccall) once the chosen file has been written into
// the WASM filesystem. `path` is an absolute MEMFS path such as
// "/uploads/foo.dsk". A null/empty path means the user cancelled.
extern "C" EMSCRIPTEN_KEEPALIVE
void gs2_web_file_selected(const char *path, uint32_t generation)
{
    if (generation != g_web_dialog_generation) return;
    SDL_DialogFileCallback cb = g_web_dialog_callback;
    void *userdata = g_web_dialog_userdata;
    g_web_dialog_callback = nullptr;
    g_web_dialog_userdata = nullptr;

    if (!cb) return;

    if (path && path[0]) {
        const char *filelist[2] = { path, nullptr };
        cb(userdata, filelist, -1);
    } else {
        // Cancel: SDL convention is a non-null list whose first entry is null.
        const char *filelist[1] = { nullptr };
        cb(userdata, filelist, -1);
    }
}

void web_open_file_dialog(SDL_DialogFileCallback callback, void *userdata, const char *accept)
{
    // Complete the superseded callback so its owner can release pending state.
    if (g_web_dialog_callback) {
        const char* canceled[] = {nullptr};
        g_web_dialog_callback(g_web_dialog_userdata, canceled, -1);
    }
    ++g_web_dialog_generation;
    g_web_dialog_callback = callback;
    g_web_dialog_userdata = userdata;

    const char *accept_attr = accept ? accept : "";

    // Create a transient <input type=file>, click it, and on change read the
    // file into /uploads/<name> then hand the path back to C.
    MAIN_THREAD_EM_ASM({
        var accept = UTF8ToString($0);
        var generation = $1;
        if (Module.gs2CloseFilePicker) Module.gs2CloseFilePicker();
        try { FS.mkdir('/uploads'); } catch (e) { /* already exists */ }
        var input = document.createElement('input');
        input.type = 'file';
        if (accept) input.accept = accept;
        input.style.display = 'none';
        document.body.appendChild(input);
        var finished = false;
        var reading = false;
        var onFocus = function () {
            setTimeout(function () {
                if (!reading && !(input.files && input.files.length)) finish('');
            }, 300);
        };
        var cleanup = function () {
            window.removeEventListener('focus', onFocus);
            if (input.parentNode) input.remove();
            if (Module.gs2CloseFilePicker === cleanup) Module.gs2CloseFilePicker = null;
        };
        Module.gs2CloseFilePicker = cleanup;
        var finish = function (path) {
            if (finished) return;
            finished = true;
            ccall('gs2_web_file_selected', null, ['string', 'number'], [path, generation]);
            cleanup();
        };
        input.addEventListener('cancel', function () { finish(''); });
        window.addEventListener('focus', onFocus);
        input.addEventListener('change', function () {
            var file = input.files && input.files[0];
            if (!file) { finish(''); return; }
            reading = true;
            var reader = new FileReader();
            reader.onload = function (event) {
                // A later picker cannot deliver this result to its callback.
                var path = '/uploads/' + generation + '-' + file.name.replace(/[\\/]/g, '_');
                try {
                    FS.writeFile(path, new Uint8Array(event.target.result));
                    finish(path);
                } catch (error) { console.error(error); finish(''); }
            };
            reader.onerror = function () { finish(''); };
            reader.readAsArrayBuffer(file);
        });
        input.click();
    }, accept_attr, g_web_dialog_generation);
}

#endif // __EMSCRIPTEN__
