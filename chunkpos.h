#ifndef CHUNKPOS_H
#define CHUNKPOS_H

class ChunkPos {
   public:
    ChunkPos(int x, int z) : x_(x), z_(z) {}
    int x() const { return this->x_; }

    int z() const { return this->z_; }

   private:
    int x_{0};
    int z_{0};
};

#endif // CHUNKPOS_H
