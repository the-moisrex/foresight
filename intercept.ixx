// Created by moisrex on 6/22/24.

module;
#include <filesystem>
export module foresight.intercept;
import foresight.evdev;

export struct interceptor {
    explicit interceptor(std::filesystem::path const& inp_path);

    int loop();

  private:
    evdev dev;
};
