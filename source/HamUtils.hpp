#pragma once

/*
Copyright 2021 Alexander Ranaldi
W2AXR
alexranaldi@gmail.com

This file is part of CWSL_DIGI.

CWSL_DIGI is free software : you can redistribute it and /or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

CWSL_DIGI is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CWSL_DIGI. If not, see < https://www.gnu.org/licenses/>.
*/

#include <string>

static inline bool isValidLocator(const std::string& loc) {
    if (loc.size() != 4) {
        return false;
    }
    if (!std::isalpha(loc.at(0))) {
        return false;
    }
    if (!std::isalpha(loc.at(1))) {
        return false;
    }
    if (!std::isdigit(loc.at(2))) {
        return false;
    }
    if (!std::isdigit(loc.at(3))) {
        return false;
    }
    return true;
}
