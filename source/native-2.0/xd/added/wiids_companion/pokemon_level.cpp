#include <game/pxdvs/app/pokemon/pokemon.hpp>
extern "C" int WiidsXD_PokemonLevel(void* p){return p?static_cast<Pokemon*>(p)->getLevel():0;}
