#ifndef WORLD_H
#define WORLD_H
#include "bedrock_level.h"

class world {
   public:
    world();

   private:
    bl::bedrock_level level_;
};

#endif  // WORLD_H
