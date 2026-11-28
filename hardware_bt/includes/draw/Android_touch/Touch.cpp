#include "release_log.hpp"
#include <cstdio>
#include <cstring>
#include <thread>
#include <memory>
#include <string>
#include <iterator>
#include <mutex>
#include <poll.h>
#include <cerrno>
#include <chrono>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <cmath>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <android/log.h>
#include "imgui.h"


namespace touch {
    struct TouchSlot {
        int raw_x = 0;
        int raw_y = 0;
        bool is_down = false;
    };
    static TouchSlot slots[10];

    struct input {
        int fd{-1};
        std::atomic<bool> canBeUsed{false};
        std::recursive_mutex mutex;
        input_absinfo absX{}, absY{};
        float absXMultiplier{0}, absYMultiplier{0};

        input(const char *_path, int32_t _screen_w, int32_t _screen_h) {
            fd = open(_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd == -1)
                return;

            canBeUsed = (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absX) != -1 &&
                         ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY) != -1 &&
                         absX.maximum > 0 && absY.maximum > 0);

            if (!canBeUsed) {
                close();
                return;
            }

            calculateMultipliers(_screen_w, _screen_h);
        }

        void calculateMultipliers(int32_t _screen_w, int32_t _screen_h) {
            auto max = fmax((float) absX.maximum, (float) absY.maximum);
            auto min = fmin((float) absX.maximum, (float) absY.maximum);
            absXMultiplier = (float) _screen_w / (_screen_w > _screen_h ? max : min);
            absYMultiplier = (float) _screen_h / (_screen_w > _screen_h ? min : max);
        }

        void close() {
            if(fd>=0){::close(fd);fd=-1;}
        }
    };

    std::atomic<uint8_t> orientation{0};
    ImGuiIO *imgui_io = nullptr;
    std::vector<std::shared_ptr<input>> inputs;
    static std::vector<std::thread> readers;
    static std::atomic<bool> is_running{true};

    // Convert raw touch hardware coordinates to display screen space
    static inline void rawToScreen(int raw_x, int raw_y, float& out_sx, float& out_sy,
                                   const input& in, uint8_t ori) {
        float mx = (in.absXMultiplier > 0.0001f) ? in.absXMultiplier : 1.0f;
        float my = (in.absYMultiplier > 0.0001f) ? in.absYMultiplier : 1.0f;

        switch (ori) {
            case 1: // Landscape (notch left)
                out_sx = (float)raw_y * my;
                out_sy = (float)(in.absX.maximum - raw_x) * mx;
                break;
            case 2: // Reverse portrait
                out_sx = (float)(in.absX.maximum - raw_x) * mx;
                out_sy = (float)(in.absY.maximum - raw_y) * my;
                break;
            case 3: // Reverse landscape (notch right)
                out_sx = (float)(in.absY.maximum - raw_y) * my;
                out_sy = (float)raw_x * mx;
                break;
            default: // Portrait (0)
                out_sx = (float)raw_x * mx;
                out_sy = (float)raw_y * my;
                break;
        }
    }

    void input_thread(std::shared_ptr<input> device) {
        auto& _input=*device;
        if (!imgui_io)
            imgui_io = &ImGui::GetIO();

        input_event events[64]{0};
        int cur_slot = 0;

        while (_input.fd >= 0 && is_running.load()) {
            pollfd descriptor{_input.fd,POLLIN,0};
            const int ready=poll(&descriptor,1,50);
            if(ready<0&&errno==EINTR)continue;
            if(ready<0||(descriptor.revents&(POLLERR|POLLHUP|POLLNVAL)))break;
            if(!ready)continue;
            std::lock_guard<std::recursive_mutex> lock(_input.mutex);
            auto event_readed_count = read(_input.fd, events, sizeof(events));
            if(event_readed_count<0&&(errno==EINTR||errno==EAGAIN))continue;
            if(event_readed_count<=0||event_readed_count%sizeof(input_event))break;

            event_readed_count /= (ssize_t) sizeof(input_event);

            for (long j = 0; j < event_readed_count; j++) {
                auto &event = events[j];

                if(event.type==EV_SYN&&event.code==SYN_DROPPED){
                    for(auto& slot:slots)slot=TouchSlot{};
                    cur_slot=0;continue;
                }
                if (event.type == EV_ABS) {
                    if (event.code == ABS_MT_SLOT) {
                        cur_slot = event.value;
                        if (cur_slot < 0 || cur_slot >= 10) cur_slot=-1;
                    }
                    else if (cur_slot>=0 && event.code == ABS_MT_TRACKING_ID) {
                        if (event.value == -1) {
                            slots[cur_slot].is_down = false;
                        } else {
                            slots[cur_slot].is_down = true;
                        }
                    }
                    else if (cur_slot>=0 && event.code == ABS_MT_POSITION_X) {
                        slots[cur_slot].raw_x = event.value;
                    }
                    else if (cur_slot>=0 && event.code == ABS_MT_POSITION_Y) {
                        slots[cur_slot].raw_y = event.value;
                    }
                }
                else if (event.type == EV_SYN && event.code == SYN_REPORT) {
                    // Update ImGui mouse state from active touches
                    int imgui_slot = -1;
                    for (int s = 0; s < 10; s++) {
                        if (slots[s].is_down) {
                            imgui_slot = s;
                            break;
                        }
                    }

                    if (imgui_io) {
                        if (imgui_slot >= 0) {
                            imgui_io->MouseDown[0] = true;
                            float sx = 0.f, sy = 0.f;
                            rawToScreen(slots[imgui_slot].raw_x, slots[imgui_slot].raw_y, sx, sy, _input, orientation);
                            imgui_io->MousePos.x = sx;
                            imgui_io->MousePos.y = sy;
                        } else {
                            imgui_io->MouseDown[0] = false;
                        }
                    }


                }
            }
        }
        device->canBeUsed=false;
    }

    bool init(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation) {
        orientation = _orientation;
        is_running.store(true);

        bool result = false;
        const char *path = "/dev/input/";
        auto dir = opendir(path);
        if (!dir) return false;

        std::vector<std::string> paths;
        dirent *ptr;
        while((ptr=readdir(dir)))if(std::strncmp(ptr->d_name,"event",5)==0)paths.push_back(std::string(path)+ptr->d_name);
        closedir(dir);
        for(const auto& event_path:paths){
            auto device=std::make_shared<input>(event_path.c_str(),_screen_w,_screen_h);
            if(!device->canBeUsed){device->close();continue;}
            // Slot state belongs to one physical touchscreen, never multiple
            // touchpads/virtual devices sharing the same global slot array.
            inputs.push_back(device);
            readers.emplace_back(input_thread,device);
            result=true;break;
        }

        return result;
    }

    void update(int32_t _screen_w, int32_t _screen_h, uint8_t _orientation) {
        orientation = _orientation;
        for (auto &_input: inputs) {
            std::lock_guard<std::recursive_mutex> lock(_input->mutex);
            _input->calculateMultipliers(_screen_w, _screen_h);
        }
    }

    void updateOrientation(uint8_t _orientation) {
        orientation = _orientation;
    }

    void shutdown() {
        is_running.store(false);
        for(auto& reader:readers)if(reader.joinable())reader.join();
        readers.clear();
        for(auto& device:inputs)device->close();
        inputs.clear();
        imgui_io = nullptr;
    }
}
