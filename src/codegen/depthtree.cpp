#include "codegen/depthtree.hpp"
#include "codegen/astnode.hpp"

#include <queue>
#include <limits>
#include <tuple>
#include <cstring>
#include <iostream>

DepthTree::DepthTree(size_t max_nodes, ASTNode* node) : max_nodes(max_nodes), max_depth(0) {
    this->node_types = new uint8_t[max_nodes];
    this->resulting_types = new uint8_t[max_nodes];
    this->parents = new uint32_t[max_nodes];
    this->depth = new uint32_t[max_nodes];
    this->child_idx = new uint32_t[max_nodes];
    this->instr_offsets = new int64_t[max_nodes];
    this->node_data = new uint32_t[max_nodes];

    std::memset(this->node_types, 0, sizeof(uint8_t) * max_nodes);
    std::memset(this->resulting_types, 0, sizeof(uint8_t) * max_nodes);
    std::memset(this->parents, -1, sizeof(uint32_t) * max_nodes);
    std::memset(this->depth, -1, sizeof(uint32_t) * max_nodes);
    std::memset(this->child_idx, -1, sizeof(uint32_t) * max_nodes);
    std::memset(this->instr_offsets, -1, sizeof(int64_t) * max_nodes);
    std::memset(this->node_data, 0, sizeof(uint32_t) * max_nodes);

    this->construct(node);
}

DepthTree::~DepthTree() {
    delete[] this->node_types;
    delete[] this->resulting_types;
    delete[] this->parents;
    delete[] this->depth;
    delete[] this->child_idx;
    delete[] this->instr_offsets;
    delete[] this->node_data;
}

void DepthTree::setElement(size_t idx, ASTNode* node, size_t parent, size_t depth, size_t child_idx) {
    this->node_types[idx] = static_cast<uint8_t>(node->getType());
    this->resulting_types[idx] = static_cast<uint8_t>(node->getResultingType());
    this->parents[idx] = parent;
    this->depth[idx] = depth;
    this->child_idx[idx] = child_idx;
    this->node_data[idx] = node->getInteger();

    if(this->max_depth < depth)
        this->max_depth = depth;
}

void DepthTree::construct(ASTNode* node) {
    std::queue<std::tuple<ASTNode*, size_t, size_t, size_t>> search_queue;
    search_queue.push(std::tuple<ASTNode*, size_t, size_t, size_t>(node, std::numeric_limits<size_t>::max(), 0, std::numeric_limits<size_t>::max()));

    std::unordered_map<ASTNode*, size_t> idx_map;

    size_t i = 0;
    while(!search_queue.empty()) {
        auto [n, parent, depth, child_idx] = search_queue.front();
        search_queue.pop();

        idx_map[n] = i;

        this->setElement(i, n, parent, depth, child_idx);

        const std::vector<ASTNode*>& children = n->getChildren();
        for(size_t j = 0; j < children.size(); ++j) {
            ASTNode* c = children[j];
            search_queue.push(std::tuple<ASTNode*, size_t, size_t, size_t>(c, i, depth + 1, j));
        }

        ++i;
    }

    size_t offset = 0;
    this->markOffset(node, idx_map, offset);

    this->filled_nodes = i;
}

void DepthTree::markOffset(ASTNode* node, const std::unordered_map<ASTNode*, size_t>& idx_map, size_t& offset) {
    for(ASTNode* n : node->getChildren()) {
        this->markOffset(n, idx_map, offset);
    }

    size_t node_idx = idx_map.at(node);

    switch(node->getType()) {
        case NodeType::INVALID:
        case NodeType::STATEMENT_LIST:
        case NodeType::EMPTY_STAT:
        case NodeType::FUNC_DECL:
        case NodeType::EXPR_STAT:
        case NodeType::IF_STAT:
        case NodeType::IF_ELSE_STAT:
        case NodeType::ELSE_AUX:
        case NodeType::WHILE_STAT:
        case NodeType::FUNC_CALL_EXPR:
        case NodeType::FUNC_CALL_ARG:
        case NodeType::LAND_EXPR:
        case NodeType::LOR_EXPR:
        case NodeType::EQ_EXPR:
        case NodeType::NEQ_EXPR:
        case NodeType::LESS_EXPR:
        case NodeType::GREAT_EXPR:
        case NodeType::LESSEQ_EXPR:
        case NodeType::GREATEQ_EXPR:
            break;
        case NodeType::ADD_EXPR:
        case NodeType::SUB_EXPR:
        case NodeType::MUL_EXPR:
        case NodeType::DIV_EXPR:
        case NodeType::MOD_EXPR:
        case NodeType::BITAND_EXPR:
        case NodeType::BITOR_EXPR:
        case NodeType::BITXOR_EXPR:
        case NodeType::LSHIFT_EXPR:
        case NodeType::RSHIFT_EXPR:
        case NodeType::URSHIFT_EXPR:
        case NodeType::BITNOT_EXPR:
        case NodeType::LNOT_EXPR:
        case NodeType::NEG_EXPR:
        case NodeType::ASSIGN_EXPR:
        case NodeType::DEREF_EXPR:
            this->instr_offsets[node_idx] = offset++;
            break;
        case NodeType::LIT_EXPR:
        case NodeType::CAST_EXPR:
        case NodeType::DECL_EXPR:
        case NodeType::ID_EXPR:
            this->instr_offsets[node_idx] = offset;
            offset += 2;
            break;
        default:
            this->instr_offsets[node_idx] = offset++;
            break;
    }
}

void DepthTree::print(std::ostream& os) const {
    for(size_t i = 0; i < this->filled_nodes; ++i) {
        os << "Node " << i << ", node type = " << static_cast<unsigned>(this->node_types[i])
            << ", data type = " << static_cast<unsigned>(this->resulting_types[i])
            << ", parent = " << this->parents[i] << ", depth = " << this->depth[i]
            << ", instr offset = " << this->instr_offsets[i] << std::endl;
    }
}

std::ostream& operator<<(std::ostream& os, const DepthTree& tree) {
    tree.print(os);
    return os;
}