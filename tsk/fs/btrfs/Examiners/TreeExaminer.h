//! \file
//! \author Shujian Yang
//!
//! Header file of class TreeExaminer.

#ifndef TREE_EXAMINER_H
#define TREE_EXAMINER_H

#include <iostream>
#include <vector>
#include <functional>
#include <tsk/libtsk.h>
#include "../Basics/Basics.h"
#include "../Trees/Trees.h"
#include "../../../pool/BTRFS_POOL.h"

namespace btrForensics {
    //! Examine a tree in btrfs.
    class TreeExaminer {
    public:
        ChunkTree* chunkTree; //!< The chunk tree.
        FilesystemTree* fsTree; //!< The file system tree.
        FilesystemTree* fsTreeDefault; //!< Default file system tree.
        const BtrfsNode* rootTree; //!< Root node of the root tree.

        TSK_IMG_INFO* image; //!< Image file.
        TSK_ENDIAN_ENUM endian; //!< Endianness.
        BTRFS_POOL* pool;

    public:
        TreeExaminer(BTRFS_POOL*, TSK_ENDIAN_ENUM, const SuperBlock*);
        TreeExaminer(BTRFS_POOL*, TSK_ENDIAN_ENUM, const SuperBlock*, uint64_t fsRootId);
        ~TreeExaminer();

        vector<BTRFSPhyAddr> getPhysicalAddr(uint64_t logicalAddr) const;
        uint64_t getChunkLogicalAddr(uint64_t logicalAddr);
        void initializeRootTree(const SuperBlock* superBlk);
        void initializeFSTree(uint64_t id = 0);
        void reInitializeFSTree(uint64_t id = 0);
        uint64_t getDefaultFsId() const;

        const void navigateNodes(const BtrfsNode* root, std::ostream& os, std::istream& is) const;
        const bool switchFsTrees(std::ostream& os, std::istream& is);

        void treeTraverse(const BtrfsNode* node,
            std::function<void(const LeafNode*)> readOnlyFunc) const;

        bool treeSearch(const BtrfsNode* node,
            std::function<bool(const LeafNode*)> searchFunc) const;

        bool treeSearchById(const BtrfsNode* node, uint64_t targetId,
            std::function<bool(const LeafNode*, uint64_t)> searchFunc) const;
    };
}

#endif
