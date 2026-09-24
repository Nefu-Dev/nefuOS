// nefuOS STL - set implementation
#pragma once

#include "vector.h"
#include "algorithm.h"

namespace nefu {
namespace stl {

template <typename Key>
class set {
private:
    vector<Key> data_;
    
public:
    typedef Key value_type;
    typedef size_t size_type;
    
    set() {}
    
    size_type size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    
    bool insert(const Key& key) {
        // Check if already exists
        for (size_t i = 0; i < data_.size(); i++) {
            if (data_[i] == key) return false;
        }
        data_.push_back(key);
        sort(data_.begin(), data_.end());
        return true;
    }
    
    bool contains(const Key& key) const {
        for (size_t i = 0; i < data_.size(); i++) {
            if (data_[i] == key) return true;
        }
        return false;
    }
    
    size_type erase(const Key& key) {
        for (size_t i = 0; i < data_.size(); i++) {
            if (data_[i] == key) {
                data_.erase(data_.begin() + i);
                return 1;
            }
        }
        return 0;
    }
    
    void clear() {
        data_.clear();
    }
    
    typename vector<Key>::iterator begin() { return data_.begin(); }
    typename vector<Key>::iterator end() { return data_.end(); }
};

} // namespace stl
} // namespace nefu
