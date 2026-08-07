#include "bpp/solution_writer.hpp"
#include <ostream>
namespace bpp { void write_solution(const Solution& solution, std::ostream& out) { out << "bins " << solution.bin_count() << '\n'; for (const auto& bin : solution.bins()) { out << bin.load << ':'; for (int item : bin.items) out << ' ' << item; out << '\n'; } } }
