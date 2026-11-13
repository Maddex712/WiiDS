#include "wiids_config.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
int main() {
    size_t count=0;
    const std::vector<std::string> valid={
        "[paths]\nnand_root='DiscData/saved-nand'",
        "[paths]\n\"nand_root\"='DiscData/saved-nand'",
        "[paths]\n'nand_root'='DiscData/saved-nand'",
        "paths.nand_root='DiscData/saved-nand'",
        "\"paths\".\"nand_root\"='DiscData/saved-nand'",
        "paths={nand_root='DiscData/saved-nand'}",
        "[paths]\n\"na\\u006ed_root\"='DiscData/saved-nand'",
        "[paths]\nnand_root='''DiscData/saved-nand'''",
        "[paths]\nnand_root=\"\"\"DiscData/saved-nand\"\"\"",
        "[paths]\nnand_root=\"DiscData/saved-nand\" # comment"
    };
    for(const auto& config:valid) {
        if(wiids::disc::ConfiguredNand(config)!="DiscData/saved-nand") throw std::runtime_error("native config mismatch");
        ++count;
    }
    for(const auto& config:{"", "[paths]\nnand_root=''", "[unrelated]\nnand_root='DiscData/saved-nand'"}) {
        if(!wiids::disc::ConfiguredNand(config).empty()) throw std::runtime_error("default config changed");
        ++count;
    }
    if(wiids::disc::ConfiguredNand("[paths]\nnand_root=\"/\\U0001F3AE#nand\"")!="/\xf0\x9f\x8e\xae#nand") throw std::runtime_error("UTF-8 config mismatch");
    ++count;
    for(const auto& config:{"[paths]\nnand_root=7", "paths=3", "[paths]\nnand_root='a'\nnand_root='b'", "[paths]\nnand_root=\"\\u0000\"", "[paths]\nnand_root=broken"}) {
        bool rejected=false;
        try { (void)wiids::disc::ConfiguredNand(config); } catch(const std::exception&) { rejected=true; }
        if(!rejected) throw std::runtime_error("invalid config accepted");
        ++count;
    }
    bool rejected=false;
    try { (void)wiids::disc::ConfiguredNand(std::string(1u<<20,'#')); } catch(const std::exception&) { rejected=true; }
    if(!rejected) throw std::runtime_error("oversize config accepted");
    ++count;
    std::printf("PASS %zu config checks\n",count);
}
