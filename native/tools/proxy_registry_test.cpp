#include "proxy_registry.hpp"
#include <cassert>
#include <iostream>
int main() {
    const std::string hash(64, 'a'), other(64, '0');
    assert(vii::prepare::ParseProxyRegistry("# header\r\n\r\n" + hash + " | release\r\n" + other + " | old\n").size() == 2);
    for (const auto& text : {std::string(), std::string("# empty\n"), hash + " | \t\n",
             std::string(64, 'A') + " | uppercase\n", hash + " release\n",
             hash + " | one\n" + hash + " | duplicate\n", hash.substr(1) + " | short\n"}) {
        bool refused = false;
        try { vii::prepare::ParseProxyRegistry(text); } catch (const std::runtime_error&) { refused = true; }
        assert(refused);
    }
    std::cout << "Registry parsing: LF/CRLF, provenance, hash syntax, duplicates and empty input pass\n";
}
