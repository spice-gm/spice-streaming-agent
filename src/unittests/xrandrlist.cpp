#include <stdio.h>

#include <spice-streaming-agent/x11-display-info.hpp>

int main()
{
    Display *display = XOpenDisplay(nullptr);

    if (!display) {
        printf("Could not open display\n");
        return 1;
    }
    Window window = RootWindow(display, XDefaultScreen(display));
    std::vector<std::string> outputs =
        spice::streaming_agent::get_xrandr_outputs(display, window);

    for (auto o : outputs) {
        printf("%s\n", o.c_str());
    }

    XCloseDisplay(display);
    return 0;
}
