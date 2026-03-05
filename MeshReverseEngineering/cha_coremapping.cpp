//
// Created by Yiwei Yang on 7/15/24.
//
#include "logging.h"
#include <algorithm>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <vector>
#include <x86intrin.h>

template <typename T, typename U> std::pair<T, U> operator+(const std::pair<T, U> &l, const std::pair<T, U> &r) {
    return {l.first + r.first, l.second + r.second};
}

static constexpr long CHA_MSR_PMON_CTRL_BASE = 0x2002L;
static constexpr long CHA_MSR_PMON_CTR_BASE = 0x2008L;

static constexpr unsigned long LEFT_READ = 0x000003b8; // HORZ_RING_BL_IN_USE.LEFT
static constexpr unsigned long RIGHT_READ = 0x00000cb8; /// horizontal_bl_ring
static constexpr unsigned long UP_READ = 0x000003b2; /// vertical_bl_ring
static constexpr unsigned long DOWN_READ = 0x00000cb2; /// vertical_bl_ring
// static constexpr unsigned long LEFT_READ =  0x0003ab;  // HORZ_RING_BL_IN_USE.LEFT
// static constexpr unsigned long RIGHT_READ = 0x000cab; /// horizontal_bl_ring
// static constexpr unsigned long UP_READ = 0x0003aa; /// vertical_bl_ring
// static constexpr unsigned long DOWN_READ = 0x000caa; /// vertical_bl_ring
static constexpr unsigned long FILTER0 = 0x00000000; /// FILTER0.NULL
static constexpr unsigned long FILTER1 = 0x0000003B; /// FILTER1.NULL

static constexpr int CACHE_LINE_SIZE = 64;
static constexpr int MAX_SOCKETS = 8;
#ifndef SPRTOP_NUM_CHA_BOXES
#define SPRTOP_NUM_CHA_BOXES 24
#endif
static constexpr int NUM_CHA_BOXES = SPRTOP_NUM_CHA_BOXES;
static constexpr int NUM_CHA_COUNTERS = 4;

uint64_t before_cha_counts[MAX_SOCKETS][NUM_CHA_BOXES][NUM_CHA_COUNTERS];
uint64_t after_cha_counts[MAX_SOCKETS][NUM_CHA_BOXES][NUM_CHA_COUNTERS];
long processor_in_socket[MAX_SOCKETS];
static std::vector<std::vector<long>> cores_per_socket;

using namespace std;

class Dir {
public:
    Dir(int);
    int dir;
    operator int();
    operator pair<int, int>();

    static const Dir UP() {
        static Dir d(2);
        return d;
    }
    static const Dir RIGHT() {
        static Dir d(1);
        return d;
    }
    static const Dir DOWN() {
        static Dir d(3);
        return d;
    }
    static const Dir LEFT() {
        static Dir d(0);
        return d;
    }
};

Dir::Dir(int dir) : dir(dir){};
Dir::operator int() { return dir; }
Dir::operator pair<int, int>() {
    switch (dir) {
        case 0:
            return pair(-1, 0);
        case 2:
            return pair(0, -1);
        case 1:
            return pair(1, 0);
        case 3:
            return pair(0, 1);
        default:
            return pair(0, 0);
    }
}

class cha_mapping {
public:
    int cha_boxes;
    int max_x, max_y;
    vector<vector<int>> possibles;

    vector<vector<int>> map_template;
    vector<int> set;
    map<int, pair<int, int>> ind_to_coord;

    cha_mapping(int cha_boxes, vector<vector<int>> map_template);
    void update_cha_mapping(int[][4], int);
    int get_set(int, int) const;
    bool data_sink(pair<int, int> coord) const;
    bool in_bounds(pair<int, int> coord) const;
};

template <> struct std::formatter<pair<int, int>> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
        // No format specifiers are supported in this example, so we simply return the end of the range.
        return ctx.end();
    }
    // Formats the Matrix object into the output.
    template <typename FormatContext>
        auto format(pair<int, int> const &m, FormatContext &&ctx) const -> decltype(ctx.out()) {
            auto it = ctx.out();
            format_to(it, "({}, {})", m.first, m.second);
            return it;
    }
}; //*/

template <> struct std::formatter<cha_mapping> {
    // Parses the format specifications passed to std::format (if any).
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
        // No format specifiers are supported in this example, so we simply return the end of the range.
        return ctx.end();
    }
    // Formats the Matrix object into the output.
    template <typename FormatContext>
    auto format(cha_mapping const &m, FormatContext &&ctx) const -> decltype(ctx.out()) {
        auto it = ctx.out();
        format_to(it, "CHA_MAPPING:\n");

        for (const auto &[index, possible] : std::views::enumerate(m.possibles)) {
            it = std::format_to(it, "{}:", index);
            for (const auto &val : possible) {
                it = std::format_to(it, "{: >2} ", val);
            }
            it = std::format_to(it, "\n");
        }
        it = std::format_to(it, "confirmed locations:\n");
        for (int y = 0; y <= m.max_y; y++) {
            for (int x = 0; x <= m.max_x; x++) {
                auto val = -1;
                if (m.map_template[y][x] != 1)
                    val = m.map_template[y][x];
                else
                    val = m.get_set(x, y);
                it = std::format_to(it, "{: >3} ", val);
            }
            it = std::format_to(it, "\n");
        }
        return it;
    }
};

int stick_this_thread_to_core(int core_id) {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores)
        return EINVAL;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}
// Detect processor_in_socket and cores_per_socket by reading physical_package_id from sysfs
static void detect_processor_in_socket(int num_sockets) {
    // Initialize to -1
    for (int i = 0; i < MAX_SOCKETS; i++) {
        processor_in_socket[i] = -1;
    }
    cores_per_socket.clear();
    cores_per_socket.resize(num_sockets);

    long logical_core_count = sysconf(_SC_NPROCESSORS_ONLN);
    for (long cpu = 0; cpu < logical_core_count; cpu++) {
        char path[256];
        sprintf(path, "/sys/devices/system/cpu/cpu%ld/topology/physical_package_id", cpu);
        FILE *f = fopen(path, "r");
        if (f) {
            int pkg_id = -1;
            if (fscanf(f, "%d", &pkg_id) == 1) {
                if (pkg_id >= 0 && pkg_id < num_sockets) {
                    cores_per_socket[pkg_id].push_back(cpu);
                    if (processor_in_socket[pkg_id] == -1) {
                        processor_in_socket[pkg_id] = cpu;
                        std::cout << "Socket " << pkg_id << " -> first CPU " << cpu << std::endl;
                    }
                }
            }
            fclose(f);
        }
    }
    // Fallback: if socket 0 not found, use cpu 0
    if (processor_in_socket[0] == -1) {
        processor_in_socket[0] = 0;
    }
}

vector<vector<int>> get_coremapping(vector<vector<int>> mapping_template, int num_active_chas, int num_sockets = 1, int socket_id = 0) {
    cha_mapping cm(num_active_chas, mapping_template);
    cout << std::format("{}", cm);
    long logical_core_count = sysconf(_SC_NPROCESSORS_ONLN);
    std::vector<int> msr_fds(logical_core_count, -1);
    char filename[100];

    detect_processor_in_socket(num_sockets);

    // Get the list of cores on the target socket for thread pinning
    const auto &socket_cores = cores_per_socket[socket_id];
    long msr_core = processor_in_socket[socket_id]; // core used for MSR reads on this socket

    /// in the first place, I will just stick thread to core0 and read data from RAM on this thread.
    std::filesystem::path file_path = std::filesystem::current_path() / "PMU.txt";

    if (std::filesystem::exists(file_path)) {
        LOG_INFO << "The file exists.\n";

        int cha_count_diffs[NUM_CHA_BOXES][NUM_CHA_COUNTERS]{};
        std::ifstream file(file_path);
        std::string line;

        if (file.is_open()) {

            for (int i = 0; i < num_active_chas; i++) {
                int cha_index = 0;
                while (std::getline(file, line) && cha_index < num_active_chas) {
                    printf("Line: %s\n", line.c_str());
                    std::istringstream iss(line);
                    std::string temp;
                    // Skip the initial Socket0-CHAxx part
                    iss >> temp; // Socket0-CHAxx:
                    // Now read the counters
                    for (int counter_index = 0; counter_index < NUM_CHA_COUNTERS; ++counter_index) {
                        iss >> temp; // left->, right->, up->, or down->
                        iss >> cha_count_diffs[cha_index][counter_index]; // Actual value
                    }
                    ++cha_index;
                }
                cm.update_cha_mapping(cha_count_diffs, 0);
                cout << std::format("{}", cm);

                // Optional: Print the array to verify
                 printf("CHA count differences for socket 0, iteration %d\n", i);
                 for (int i = 0; i < num_active_chas; ++i) {
                     for (int j = 0; j < NUM_CHA_COUNTERS; ++j) {
                         std::cout << cha_count_diffs[i][j] << " ";
                     }
                     std::cout << std::endl;
                 }
            }
        } else {
            std::cout << "Unable to open file" << std::endl;
        }
    } else {
        // Try to load the msr kernel module if not already loaded
        (void)system("modprobe msr 2>/dev/null");

        std::cout << "Logical core count: " << logical_core_count << "\n";
        bool msr_available = true;
        for (auto i = 0; i < logical_core_count; ++i) {
            sprintf(filename, "/dev/cpu/%d/msr", i);
            int fd = open(filename, O_RDWR);
            if (fd == -1) {
                std::cerr << "WARNING: could not open " << filename
                          << " - CHA probing disabled (try: sudo modprobe msr)\n";
                msr_available = false;
                break;
            } else {
                msr_fds[i] = fd;
            }
        }

        if (msr_available) {
            uint64_t msr_val = 0;
            uint64_t msr_num = 0;
            ssize_t rc64 = 0;
            std::vector<unsigned long> counters{LEFT_READ, RIGHT_READ, UP_READ, DOWN_READ};

            // Configure CHA counters only on the target socket
            for (int cha = 0; cha < num_active_chas; ++cha) {
                long core = msr_core;

                for (int counter = 0; counter < (int)counters.size(); ++counter) {
                    msr_num = 0x2000 + 0x10 * cha;
                    msr_val = FILTER0;
                    rc64 = pwrite(msr_fds[core], &msr_val, sizeof(msr_val), msr_num);
                    std::cout << rc64 << "\n";

                    msr_num = 0x200e + 0x10 * cha;
                    msr_val = FILTER1;
                    rc64 = pwrite(msr_fds[core], &msr_val, sizeof(msr_val), msr_num);
                    std::cout << rc64 << "\n";

                    msr_val = counters[counter];
                    msr_num = CHA_MSR_PMON_CTRL_BASE + (0x10 * cha) + counter;

                    rc64 = pwrite(msr_fds[core], &msr_val, sizeof(msr_val), msr_num);
                    if (rc64 < 0) {
                        fprintf(stdout, "ERROR writing to MSR device on core %ld, write %zd bytes\n", core, rc64);
                        exit(EXIT_FAILURE);
                    } else {
                        cout << "Configuring socket" << socket_id << "-CHA" << cha << " by writing 0x" << std::hex
                             << msr_val << " to core " << std::dec << core << ", offset 0x" << std::hex << msr_num
                             << std::dec << "\n";
                    }
                }
            }

            /// create 2GB of data in RAM that would be accessed by this socket's core in the next step.
            std::vector<int> data(536870912);

            /// Flush the data from the cache
           for (size_t i = 0; i < data.size(); i = i + CACHE_LINE_SIZE) {
               _mm_clflush(&data[i]);
           }

            // Iterate over cores on the TARGET socket only
            int num_probes = std::min((int)socket_cores.size(), num_active_chas);
            for (int lproc = 0; lproc < num_probes; lproc++) {
                long actual_core = socket_cores[lproc];
                LOG_INFO << "Sticking main thread to core " << actual_core << " (socket " << socket_id << " core " << lproc << ")\n";
                stick_this_thread_to_core(actual_core);

                LOG_DEBUG << "---------------- FIRST READINGS ----------------"
                          << "\n";
                {
                    long core = msr_core;
                    for (int cha = 0; cha < num_active_chas; ++cha) {
                        for (int counter = 0; counter < NUM_CHA_COUNTERS; ++counter) {
                            msr_num = CHA_MSR_PMON_CTR_BASE + (0x10 * cha) + counter;
                            rc64 = pread(msr_fds[core], &msr_val, sizeof(msr_val), msr_num);
                            if (rc64 != sizeof(msr_val)) {
                                exit(EXIT_FAILURE);
                            } else {
                                LOG_DEBUG << "Read " << msr_val << " from socket" << socket_id << "-CHA" << cha << " on core "
                                          << core << ", offset 0x" << std::hex << msr_num << std::dec << "\n";
                                before_cha_counts[socket_id][cha][counter] = msr_val;
                            }
                        }
                    }
                }

                /// I am basically fetching data from RAM to cache here.
                for (auto &val : data) {
                    val += 5;
                }

                LOG_DEBUG << "---------------- SECOND READINGS ----------------"
                          << "\n";
                {
                    long core = msr_core;
                    for (int cha = 0; cha < num_active_chas; ++cha) {
                        for (int counter = 0; counter < NUM_CHA_COUNTERS; ++counter) {
                            msr_num = CHA_MSR_PMON_CTR_BASE + (0x10 * cha) + counter;
                            rc64 = pread(msr_fds[core], &msr_val, sizeof(msr_val), msr_num);
                            if (rc64 != sizeof(msr_val)) {
                                exit(EXIT_FAILURE);
                            } else {
                                LOG_DEBUG << "Read " << msr_val << " from socket" << socket_id << "-CHA" << cha << " on core "
                                          << core << ", offset 0x" << std::hex << msr_num << std::dec << "\n";
                                after_cha_counts[socket_id][cha][counter] = msr_val;
                            }
                        }
                    }
                }

                LOG_INFO << "---------------- TRAFFIC ANALYSIS ----------------"
                         << "\n";

                int cha_count_diffs[NUM_CHA_BOXES][NUM_CHA_COUNTERS]{};

                for (int cha = 0; cha < num_active_chas; ++cha) {
                    LOG_INFO << "Socket" << socket_id << '-' << "CHA" << cha << ": ";
                    for (int counter = 0; counter < NUM_CHA_COUNTERS; ++counter) {
                        if (counter == 0) {
                            LOG_INFO << "left->";
                        } else if (counter == 1) {
                            LOG_INFO << "right->";
                        } else if (counter == 2) {
                            LOG_INFO << "up->";
                        } else if (counter == 3) {
                            LOG_INFO << "down->";
                        }

                        cha_count_diffs[cha][counter] =
                                after_cha_counts[socket_id][cha][counter] - before_cha_counts[socket_id][cha][counter];
                        LOG_INFO << setw(10) << cha_count_diffs[cha][counter] << "\t";
                    }
                    LOG_INFO << "\n";
                }

                cm.update_cha_mapping(cha_count_diffs, lproc);
                cerr << std::format("{}", cm);
            }
        } else {
            std::cerr << "WARNING: MSR devices unavailable, skipping CHA probing.\n"
                      << "Topology grid is still valid. CHA-to-core mapping will be unresolved.\n";
        }
    }
    // Build result grid from analysis: encode CHA IDs as -(cha_id + 1)
    // so CHA 0 -> -1, CHA 1 -> -2, etc. Non-core positions keep their template values.
    vector<vector<int>> result = mapping_template;
    for (int cha = 0; cha < num_active_chas; cha++) {
        if (cm.possibles[cha].size() == 1) {
            int phys_index = cm.possibles[cha][0];
            auto [x, y] = cm.ind_to_coord[phys_index];
            result[y][x] = -(cha + 1);
        }
    }
    return result;
}

cha_mapping::cha_mapping(int cha_boxes, vector<vector<int>> map_template)
        : cha_boxes(cha_boxes), map_template(map_template) {
    int phys_box = 0;
    max_x = max_y = 0;
    for (auto [indi, mtl] : std::views::enumerate(map_template)) {
        for (auto [indj, i] : std::views::enumerate(mtl)) {
            if (i == 1) {
                ind_to_coord[phys_box] = pair(indj, indi);
                phys_box++;
                set.emplace_back(-1);
            }
            if (max_x < indj)
                max_x = indj;
        }
        if (max_y < indi)
            max_y = indi;
    }
    for (int i = 0; i < cha_boxes; i++) {
        possibles.emplace_back();
        for (int j = 0; j < phys_box; j++) {
            possibles[i].emplace_back(j);
        }
    }
}
void cha_mapping::update_cha_mapping(int counts[][NUM_CHA_COUNTERS], int focus) {
    bool data_movement[cha_boxes][4];
    bool data_impossible[cha_boxes][4];
    // populate movement and possible
    for (int i = 0; i < cha_boxes; i++) {
        int total = 0;
        for (int j = 0; j < 4; j++) {
            total += counts[i][j];
        }
        total /= 4;

        for (int j = 0; j < 4; j++) {
            data_movement[i][j] = total < counts[i][j];
            data_impossible[i][j] = counts[i][j] < 1000;
        }
    }
    // check for edges
    for (int i = 0; i < cha_boxes; i++) {
        for (int j = 0; j < 4; j++) {
            possibles[i].erase( //
                    std::remove_if( //
                            possibles[i].begin(), possibles[i].end(),
                            [&](int ind) {
                                auto coord = ind_to_coord[ind];

                                auto status = // if we aren't sending anything and we should be able to send
                                        (data_impossible[i][j] && (in_bounds(coord + (pair<int, int>)Dir(j)) &&
                                                                   data_sink(coord + (pair<int, int>)Dir(j)))) ||
                                        // or we are sending and we shouldn't be able to
                                        (data_movement[i][j] &&
                                         !(in_bounds(coord + (pair<int, int>)Dir(j)) && data_sink(coord + (pair<int, int>)Dir(j))));
                                return status;
                            }),
                    possibles[i].end());
        }
    }
    // check for flow — skip if possibles[focus] is empty (prevents cascade wipeout)
    if (focus >= 0 && focus < cha_boxes && !possibles[focus].empty()) {
         for (int i = 0; i < cha_boxes; i++) {
             int dir = 0;
             for (int j = 1; j < 4; j++) {
                 if (counts[i][j] > counts[i][dir]) {
                     dir = j;
                 }
             }
             pair<int, int> moveDir = Dir(dir);
             int max_dirx = -max_x, max_diry = -max_y;
             for (auto index : possibles[focus]) {
                 if (max_dirx < ind_to_coord[index].first * -moveDir.first) {
                     max_dirx = ind_to_coord[index].first * -moveDir.first;
                 }
                 if (max_diry < ind_to_coord[index].second * -moveDir.second) {
                     max_diry = ind_to_coord[index].second * -moveDir.second;
                 }
             }
             if (max_dirx < 0 || max_diry < 0) {
                 max_dirx *= -1;
                 max_diry *= -1;
             }

             possibles[i].erase( //
                 std::remove_if( //
                     possibles[i].begin(), possibles[i].end(),
                     [&](int ind) {
                         auto coord = ind_to_coord[ind];
                         bool status = false;
                         if (max_dirx == 0) {
                             // handle verticals
                             return max_diry * moveDir.second <= coord.second * moveDir.second;
                         } else {
                              return max_dirx * moveDir.first <= coord.first * moveDir.first;
                             // or horizontals
                         }

                         return status;
                     }),
                 possibles[i].end());
         }
    }
}

bool cha_mapping::in_bounds(pair<int, int> coord) const {

    return coord.first >= 0 && coord.second >= 0 && coord.first <= max_x && coord.second <= max_y;
}
bool cha_mapping::data_sink(pair<int, int> coord) const {
    int v = map_template[coord.second][coord.first];
    // 1=active core, 2=IMC, 3=UPI, 4=PCIe, 5=disabled core — all have mesh stops
    return v == 1 || v == 2 || v == 3 || v == 4 || v == 5;
}
int cha_mapping::get_set(int x, int y) const {
    for (auto &[ind, p] : ind_to_coord) {
        auto [px, py] = p;
        if (px == x && py == y)
            return set[ind];
    }
    return -1;
}
