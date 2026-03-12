#include <scinterm_notcurses.h>
#include <notcurses/notcurses.h>

// Notification callback
void callback(void *sci, int iMessage, SCNotification *n, void *userdata) {
    // Handle Scintilla notifications
}

int main() {
    // Initialize NotCurses
    scintilla_notcurses_init();
    struct notcurses *nc = notcurses_get();
    
    // Create editor
    void *editor = scintilla_new(callback, NULL);
    
    // Set initial text
    scintilla_send_message(editor, SCI_SETTEXT, 0, 
        (sptr_t)"Hello, Scinterm World!\n");
    
    // Main loop
    bool running = true;
    while (running) {
        scintilla_render(editor);
        notcurses_render(nc);
        
        ncinput input;
        uint32_t key = notcurses_get_blocking(nc, &input);
        
        if (key == NCKEY_ESC) {
            running = false;
        } else {
            scintilla_process_input(editor, nc);
        }
    }
    
    // Cleanup
    scintilla_delete(editor);
    scintilla_notcurses_shutdown();
    
    return 0;
}
