#include "ChannelSwitchCompletion.h"
#include <cstdio>
#include <thread>

int main() {
    devourer::ChannelSwitchCompletion completion;
    const uint8_t event[] = {0x10, 1};
    const uint8_t other[] = {0xff, 1, 0x10};
    completion.notify(event, sizeof(event)); // unsolicited/late event
    completion.arm();
    completion.notify(other, sizeof(other)); // extended event must not complete
    completion.notify(event, 1); // truncated
    if (completion.wait(1)) return 1;
    completion.arm();
    completion.notify(event, sizeof(event)); // event before the waiter enters
    if (!completion.wait(10)) return 2;
    completion.arm();
    std::thread rx([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        completion.notify(event, sizeof(event));
    });
    const bool completed = completion.wait(100);
    rx.join();
    if (!completed) return 3;
    completion.arm(); // previous completion must not leak into the next hop
    if (completion.wait(1)) return 4;
    std::puts("PASS: C2H bounds/type, pre-wait notification, asynchronous wake, timeout and re-arm");
}
