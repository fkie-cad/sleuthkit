//! \file
//! \author Shujian Yang
//!
//! Header file of class BtrfsNode.

#ifndef INTERNAL_NODE_H
#define INTERNAL_NODE_H

#include <vector>
#include <tsk/libtsk.h>
#include "BtrfsNode.h"
#include "../../../pool/BTRFS_POOL.h"

using std::vector;

namespace btrForensics{
    class KeyPtr;
    //! Internal node in B-tree structure.
    class InternalNode : public BtrfsNode {
    public:
        vector<KeyPtr*> keyPointers; //!< Key pointers to other nodes.

    public:
        InternalNode(BTRFS_POOL*, const BtrfsHeader*, TSK_ENDIAN_ENUM, uint64_t);
        ~InternalNode();

        const std::string info() const override;
    };

}

#endif

