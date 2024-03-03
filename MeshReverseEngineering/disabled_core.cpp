#include <bitset>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <array>

// Function to convert CAPID6 register value to a binary string representation
std::string capid6ToBinaryString(unsigned long capid6Value) {
    return std::bitset<34>(capid6Value).to_string(); // 34 bits for your processor's CAPID6 register
}
std::vector<std::vector<int>> transpose(std::vector<std::vector<int>> &mappingTemplate) {
    std::vector<std::vector<int>> newMappingTemplate;
    for (int i = 0; i < mappingTemplate.size(); i++) {
        std::vector<int> row;
        for (int j = 0; j < mappingTemplate[i].size(); j++) {
            row.push_back(mappingTemplate[j][i]);
        }
        newMappingTemplate.push_back(row);
    }
    return newMappingTemplate;
}

// Function to update the mapping template based on the CAPID6 register's binary string
void updateMappingTemplate(std::vector<std::vector<int>> &mappingTemplate, const std::string &binaryString) {
    // Assuming the binaryString is directly mapping to the cores in the template
    // This is a simplified example, you might need to adjust the logic based on your actual mapping
    int binaryIndex = 0; // Start from the least significant bit
    for (auto &row : mappingTemplate) {
        for (auto &cell : row) {
            if (cell == 1) { // If it's a core location
                if (binaryString[binaryIndex++] == '0') { // If the corresponding bit is 0, mark as disabled
                    cell = 5; // Marking the disabled core
                }
            }
        }
    }
}

std::string exec(const char *cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string parseLscpuOutput(const std::string &output, const std::string &searchTerm) {
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find(searchTerm) != std::string::npos) {
            return line;
        }
    }
}

std::vector<std::vector<int>> & disabled_core(std::vector<std::vector<int>> &mappingTemplate) {

    // Example CAPID6 value from your setpci command
    // if 34 bits
    std::string lspciResult = exec("lspci | grep 1e.3");
    std::regex deviceRegex("(\\w+:\\w+\\.\\w+)"); // Regex to extract device ID
    std::smatch matches;
    std::string setpciResult;
    // Use regex to find device IDs in the output
    while (std::regex_search(lspciResult, matches, deviceRegex)) {
        for (const auto &match : matches) {
            std::string device = match.str();
            std::cout << "Found device: " << device << std::endl;

            // Building the setpci command with the found device ID
            std::string setpciCmd = "sudo setpci -s " + device + " 0x9c.l";
            setpciResult = exec(setpciCmd.c_str());

            // Output the result of the setpci command
            std::cout << "Result of setpci for device " << device << ": " << setpciResult << std::endl;
        }
        lspciResult = matches.suffix().str(); // Continue searching in the rest of the string
    }
    unsigned long capid6Value = atoi(setpciResult.c_str());
    std::string binaryString = capid6ToBinaryString(capid6Value);
    binaryString[0] = '1'; // padding first two core
    binaryString[1] = '1';
    auto transposedTemplate = transpose(mappingTemplate);
    // Update the mapping template based on the binary string
    updateMappingTemplate(transposedTemplate, binaryString);
    mappingTemplate = transpose(transposedTemplate);
    // Print the updated mapping template
    for (const auto &row : mappingTemplate) {
        for (const auto &cell : row) {
            std::cout << cell << " ";
        }
        std::cout << std::endl;
    }
    return mappingTemplate;
}