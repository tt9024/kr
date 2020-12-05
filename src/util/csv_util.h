#pragma once

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <stdexcept>
#include <vector>

#include <stdio.h>
#include <cstddef>
#include <cstdlib>

namespace utils {
    class CSVUtil {
    public:
        using LineTokens = std::vector<std::string>;
        using FileTokens = std::vector<LineTokens>;
        static const char Delimiter = ',';

        static LineTokens read_line(const std::string& line, char delimiter = Delimiter) {
            LineTokens vec;
            if (line.size()==0) 
                return vec;

            std::string line0 = line;
            if (line0.back()==delimiter) {
                //in case last column skipped
                line0.append(1, ' ');
            }
            std::istringstream iss(line0);
            std::string token;
            while (getline(iss, token, delimiter)) {
                std::string tk;
                std::istringstream issl(token);
                issl>>std::skipws;
                issl>>std::ws;
                issl>>tk;
                vec.push_back(tk);
            }
            return vec;
        }

        static FileTokens read_file(const std::string& csv_file, char delimiter = Delimiter, int skip_head_lines = 0) {
            std::string line;
            FileTokens vec;
            try {
                std::ifstream csv(csv_file);
                while (std::getline(csv, line)) {
                    const auto v = read_line(line, delimiter);
                    if (v.size()>0) {
                        vec.push_back(v);
                    }
                }
            } catch(const std::exception& e) {
                fprintf(stderr, "Error getting csv file %s: %s\n", csv_file.c_str(), e.what());
            }
            return vec;
        }

        static void write_line(const LineTokens& token_vec, std::ofstream& csvfile, char delimiter = Delimiter) {
            if (token_vec.size()==0) 
                return;
            csvfile << token_vec[0];
            for (size_t i=1; i<token_vec.size();++i) {
                csvfile << delimiter << token_vec[i];
            }
            csvfile << std::endl;
        }

        static bool write_file(const FileTokens& line_vec, const std::string& filename, bool append=true,  char delimiter=Delimiter) {
            std::ofstream csvfile;
            try {
                if (append) 
                    csvfile.open(filename, std::ios_base::app);
                else {
                    csvfile.open(filename, std::ios_base::trunc);
                }
                 
                for (const auto& line : line_vec) {
                     write_line(line, csvfile, delimiter);
                }
                return true;
            } catch (const std::exception& e) {
                fprintf(stderr, "csv file %s write failed: %s\n", filename.c_str(), e.what());
            } catch (...) {
                fprintf(stderr, "csv file %s write failed for unknown reason\n", filename.c_str());
            };
            return false;
        };

        static bool write_line(const LineTokens& token_vec, const std::string& filename, bool append=true,  char delimiter=Delimiter) {
            FileTokens ft;
            ft.push_back(token_vec);
            return write_file(ft, filename, append, delimiter);
        };
    };
}


