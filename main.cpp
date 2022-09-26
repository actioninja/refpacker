#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <filesystem>
#include <sys/stat.h>
#include "refpack.cpp"

using namespace std;

namespace fs = std::filesystem;

bool cmdOptionExists(char** begin, char**end, const string& option) {
    return find(begin, end, option) != end;
}

char* getCmdOption(char** begin, char** end, const string& option) {
    char** itr = find(begin, end, option);
    if (itr != end && ++itr != end) {
        return *itr;
    }
    return 0;
}

int main(int argc, char *argv[]) {

    const char* filename = getCmdOption(argv, argv + argc, "-f");

    if (!filename) {
        cout << "Needs filename passed as -f";
        return 1;
    }

    const string filename_string(filename);

    ifstream inFile(filename, ios_base::binary);

    if (!inFile.good()) {
        cout << "File not found";
        return 1;
    }

    vector<char> buffer((istreambuf_iterator<char>(inFile)), istreambuf_iterator<char>());



    //SIN AND HATRED
    auto* buf_ptr = reinterpret_cast<uint8_t *>(buffer.data());

    void* out_buf;
    uint32_t out_len;

    if (cmdOptionExists(argv, argv + argc, "-d")) {
        DecompressorInput in{};
        in.buffer = buf_ptr;
        in.lengthInBytes = buffer.size();
        cout << "Decompressor mode";

        CompressorInput out{};

        decompress(in, &out);

        out_buf = out.buffer;
        out_len = out.lengthInBytes;


    } else {
        CompressorInput in{};
        in.buffer = buf_ptr;
        in.lengthInBytes = buffer.size();

        DecompressorInput out{};

        compress(in, &out);

        out_buf = out.buffer;
        out_len = out.lengthInBytes;
    }

    const auto* byte_buf = (uint8_t*) out_buf;
    vector<uint8_t> vec_buf(byte_buf, byte_buf + out_len);

    fs::create_directory("out");

    const string out_filename = "out/" + filename_string;

    ofstream output_file(out_filename);
    ostream_iterator<uint8_t> output_iterator(output_file);
    copy(vec_buf.begin(), vec_buf.end(), output_iterator);

    return 0;
}
