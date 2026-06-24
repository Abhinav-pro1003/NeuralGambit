#include <cctype>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

class PuzzleSolver {
public:
    explicit PuzzleSolver(const std::string& json_path) {
        load(json_path);
    }

    bool has_position(const std::string& fen) const {
        return puzzles.find(fen) != puzzles.end();
    }

    std::string solution_for(const std::string& fen) const {
        auto it = puzzles.find(fen);
        if (it == puzzles.end()) {
            throw std::out_of_range("FEN not found in puzzle database");
        }
        return it->second;
    }

    std::string first_move_for(const std::string& fen) const {
        return first_move(solution_for(fen));
    }

    std::size_t size() const {
        return puzzles.size();
    }

private:
    std::unordered_map<std::string, std::string> puzzles;

    static std::string read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Could not open " + path);
        }
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    static void skip_ws(const std::string& text, std::size_t& i) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
    }

    static std::string parse_json_string(const std::string& text, std::size_t& i) {
        if (i >= text.size() || text[i] != '"') {
            throw std::runtime_error("Expected JSON string");
        }
        ++i;

        std::string result;
        while (i < text.size()) {
            char ch = text[i++];
            if (ch == '"') {
                return result;
            }
            if (ch == '\\') {
                if (i >= text.size()) {
                    throw std::runtime_error("Invalid JSON escape");
                }
                char escaped = text[i++];
                switch (escaped) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    default: result.push_back(escaped); break;
                }
            } else {
                result.push_back(ch);
            }
        }
        throw std::runtime_error("Unterminated JSON string");
    }

    void load(const std::string& path) {
        const std::string text = read_file(path);
        std::size_t i = 0;
        skip_ws(text, i);
        if (i >= text.size() || text[i] != '{') {
            throw std::runtime_error("Expected top-level JSON object");
        }
        ++i;

        while (true) {
            skip_ws(text, i);
            if (i < text.size() && text[i] == '}') {
                ++i;
                break;
            }

            std::string fen = parse_json_string(text, i);
            skip_ws(text, i);
            if (i >= text.size() || text[i] != ':') {
                throw std::runtime_error("Expected ':' after FEN");
            }
            ++i;
            skip_ws(text, i);
            std::string solution = parse_json_string(text, i);
            puzzles[fen] = solution;

            skip_ws(text, i);
            if (i < text.size() && text[i] == ',') {
                ++i;
                continue;
            }
            if (i < text.size() && text[i] == '}') {
                ++i;
                break;
            }
            throw std::runtime_error("Expected ',' or '}'");
        }
    }

    static std::string first_move(std::string line) {
        const std::string black_prefix = "1... ";
        const std::string white_prefix = "1. ";

        if (line.rfind(black_prefix, 0) == 0) {
            line.erase(0, black_prefix.size());
        } else if (line.rfind(white_prefix, 0) == 0) {
            line.erase(0, white_prefix.size());
        }

        std::size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
            ++start;
        }
        std::size_t end = start;
        while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) {
            ++end;
        }
        return line.substr(start, end - start);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: puzzle_solver <mate_in_json> \"<fen>\" [--line]\n";
        return 1;
    }

    try {
        PuzzleSolver solver(argv[1]);
        const std::string fen = argv[2];
        if (!solver.has_position(fen)) {
            std::cerr << "FEN not found. Loaded " << solver.size() << " puzzles.\n";
            return 2;
        }

        if (argc >= 4 && std::string(argv[3]) == "--line") {
            std::cout << solver.solution_for(fen) << '\n';
        } else {
            std::cout << solver.first_move_for(fen) << '\n';
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
