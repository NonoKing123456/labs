#include <cassert>
#include <algorithm>  
#include <vector>

#include "EStore.h"

using namespace std;


Item::
Item() : valid(false)
{ }

Item::
~Item()
{ }


EStore::
EStore(bool enableFineMode)
    : fineMode(enableFineMode)
{
    smutex_init(&mtx);
    for (int i = 0; i < INVENTORY_SIZE; ++i) scond_init(&item_cv[i]);

    // fine-grained
    for (int i = 0; i < INVENTORY_SIZE; ++i) {
        smutex_init(&item_mtx[i]);
        scond_init(&item_cv_fine[i]);
    }
    smutex_init(&global_mtx);
}

EStore::
~EStore()
{
    for (int i = 0; i < INVENTORY_SIZE; ++i) scond_destroy(&item_cv[i]);
    smutex_destroy(&mtx);

    // fine-grained
    for (int i = 0; i < INVENTORY_SIZE; ++i) {
        scond_destroy(&item_cv_fine[i]);
        smutex_destroy(&item_mtx[i]);
    }
    smutex_destroy(&global_mtx);
}

/*
 * ------------------------------------------------------------------
 * buyItem --
 *
 *      Attempt to buy the item from the store.
 *
 *      An item can be bought if:
 *          - The store carries it.
 *          - The item is in stock.
 *          - The cost of the item plus the cost of shipping is no
 *            more than the budget.
 *
 *      If the store *does not* carry this item, simply return and
 *      do nothing. Do not attempt to buy the item.
 *
 *      If the store *does* carry the item, but it is not in stock
 *      or its cost is over budget, block until both conditions are
 *      met (at which point the item should be bought) or the store
 *      removes the item from sale (at which point this method
 *      returns).
 *
 *      The overall cost of a purchase for a single item is defined
 *      as the current cost of the item times 1 - the store
 *      discount, plus the flat overall store shipping fee.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
buyItem(int item_id, double budget)
{
    assert(!fineModeEnabled());
    if (item_id < 0 || item_id >= INVENTORY_SIZE) return;

    smutex_lock(&mtx);

    // If the store doesn't carry it, do nothing and return.
    if (!inventory[item_id].valid) {
        smutex_unlock(&mtx);
        return;
    }

    // Wait while the store still carries it, but it’s either OOS or over budget.
    while (inventory[item_id].valid) {
        Item &it = inventory[item_id];
        double total = totalCost_nolock(it);

        if (it.quantity > 0 && total <= budget) {
            // Buy it
            it.quantity -= 1;
            smutex_unlock(&mtx);
            return;
        }
        // Sleep until something about this item changes (stock/price/discount) or it’s removed.
        scond_wait(&item_cv[item_id], &mtx);
    }

    // If we reach here, the item was removed while we waited.
    smutex_unlock(&mtx);
}

/*
 * ------------------------------------------------------------------
 * buyManyItem --
 *
 *      Attempt to buy all of the specified items at once. If the
 *      order cannot be bought, give up and return without buying
 *      anything. Otherwise buy the entire order at once.
 *
 *      The entire order can be bought if:
 *          - The store carries all items.
 *          - All items are in stock.
 *          - The cost of the entire order (cost of items plus
 *            shipping for each item) is no more than the budget.
 *
 *      If multiple customers are attempting to buy at the same
 *      time and their orders are mutually exclusive (i.e., the
 *      two customers are not trying to buy any of the same items),
 *      then their orders must be processed at the same time.
 *
 *      For the purposes of this lab, it is OK for the store
 *      discount and shipping cost to change while an order is being
 *      processed.
 *
 *      The cost of a purchase of many items is the sum of the
 *      costs of purchasing each item individually. The purchase
 *      cost of an individual item is covered above in the
 *      description of buyItem.
 *
 *      Challenge: For bonus points, implement a version of this
 *      method that will wait until the order can be fulfilled
 *      instead of giving up. The implementation should be efficient
 *      in that it should not wake up threads unecessarily. For
 *      instance, if an item decreases in price, only threads that
 *      are waiting to buy an order that includes that item should be
 *      signaled (though all such threads should be signaled).
 *
 *      Challenge: For bonus points, ensure that the shipping cost
 *      and store discount does not change while processing an
 *      order.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
buyManyItems(vector<int>* item_ids, double budget)
{
   assert(fineModeEnabled());
    if (!item_ids || item_ids->empty()) return;

    std::vector<int> ids;
    ids.reserve(item_ids->size());
    for (int id : *item_ids) {
        if (0 <= id && id < INVENTORY_SIZE) ids.push_back(id);
    }
    if (ids.empty()) return;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    double shipSnap, discSnap;
    smutex_lock(&global_mtx);
    shipSnap = shippingCost;
    discSnap = storeDiscount;
    smutex_unlock(&global_mtx);

    for (int id : ids) {
        smutex_lock(&item_mtx[id]);
    }

    bool ok = true;
    double total = 0.0;
    for (int id : ids) {
        const Item& it = inventory[id];
        if (!it.valid || it.quantity <= 0) { ok = false; break; }
        double itemPrice = it.price * (1.0 - it.discount);
        double perItemCost = itemPrice * (1.0 - discSnap) + shipSnap;
        if (perItemCost < 0.0) { ok = false; break; }
        total += perItemCost;
        if (total > budget) { ok = false; break; } // early abort
    }

    // 5) Commit or abort
    if (ok) {
        for (int id : ids) {
            inventory[id].quantity -= 1;
        }
    }

    for (int i = static_cast<int>(ids.size()) - 1; i >= 0; --i) {
        smutex_unlock(&item_mtx[ids[i]]);
    }
}

/*
 * ------------------------------------------------------------------
 * addItem --
 *
 *      Add the item to the store with the specified quantity,
 *      price, and discount. If the store already carries an item
 *      with the specified id, do nothing.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
addItem(int item_id, int quantity, double price, double discount)
{
    if (item_id < 0 || item_id >= INVENTORY_SIZE) return;

    if (!fineMode) {
        smutex_lock(&mtx);
        Item &it = inventory[item_id];
        if (!it.valid) {
            it.valid = true; it.quantity = quantity; it.price = price; it.discount = discount;
            scond_broadcast(&item_cv[item_id], &mtx);
        }
        smutex_unlock(&mtx);
    } else {
        smutex_lock(&item_mtx[item_id]);
        Item &it = inventory[item_id];
        if (!it.valid) {
            it.valid = true; it.quantity = quantity; it.price = price; it.discount = discount;
            scond_broadcast(&item_cv_fine[item_id], &item_mtx[item_id]);
        }
        smutex_unlock(&item_mtx[item_id]);
    }
}

/*
 * ------------------------------------------------------------------
 * removeItem --
 *
 *      Remove the item from the store. The store no longer carries
 *      this item. If the store is not carrying this item, do
 *      nothing.
 *
 *      Wake any waiters.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
removeItem(int item_id)
{
    if (item_id < 0 || item_id >= INVENTORY_SIZE) return;

    if (!fineMode) {
        smutex_lock(&mtx);
        Item &it = inventory[item_id];
        if (it.valid) { it.valid = false; scond_broadcast(&item_cv[item_id], &mtx); }
        smutex_unlock(&mtx);
    } else {
        smutex_lock(&item_mtx[item_id]);
        Item &it = inventory[item_id];
        if (it.valid) { it.valid = false; scond_broadcast(&item_cv_fine[item_id], &item_mtx[item_id]); }
        smutex_unlock(&item_mtx[item_id]);
    }
}

/*
 * ------------------------------------------------------------------
 * addStock --
 *
 *      Increase the stock of the specified item by count. If the
 *      store does not carry the item, do nothing. Wake any waiters.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
addStock(int item_id, int count)
{
    if (item_id < 0 || item_id >= INVENTORY_SIZE) return;

    if (!fineMode) {
        smutex_lock(&mtx);
        Item &it = inventory[item_id];
        if (it.valid && count > 0) { it.quantity += count; scond_broadcast(&item_cv[item_id], &mtx); }
        smutex_unlock(&mtx);
    } else {
        smutex_lock(&item_mtx[item_id]);
        Item &it = inventory[item_id];
        if (it.valid && count > 0) { it.quantity += count; scond_broadcast(&item_cv_fine[item_id], &item_mtx[item_id]); }
        smutex_unlock(&item_mtx[item_id]);
    };
}
/*
 * ------------------------------------------------------------------
 * priceItem --
 *
 *      Change the price on the item. If the store does not carry
 *      the item, do nothing.
 *
 *      If the item price decreased, wake any waiters.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
priceItem(int item_id, double price)
{
    if (item_id < 0 || item_id >= INVENTORY_SIZE) return;

    if (!fineMode) {
        smutex_lock(&mtx);
        Item &it = inventory[item_id];
        if (it.valid) {
            bool decreased = (price < it.price);
            it.price = price;
            if (decreased) scond_broadcast(&item_cv[item_id], &mtx);
        }
        smutex_unlock(&mtx);
    } else {
        smutex_lock(&item_mtx[item_id]);
        Item &it = inventory[item_id];
        if (it.valid) {
            bool decreased = (price < it.price);
            it.price = price;
            if (decreased) scond_broadcast(&item_cv_fine[item_id], &item_mtx[item_id]);
        }
        smutex_unlock(&item_mtx[item_id]);
    }
}
/*
 * ------------------------------------------------------------------
 * discountItem --
 *
 *      Change the discount on the item. If the store does not carry
 *      the item, do nothing.
 *
 *      If the item discount increased, wake any waiters.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
discountItem(int item_id, double discount)
{
    if (item_id < 0 || item_id >= INVENTORY_SIZE) return;

    if (!fineMode) {
        smutex_lock(&mtx);
        Item &it = inventory[item_id];
        if (it.valid) {
            bool increased = (discount > it.discount);
            it.discount = discount;
            if (increased) scond_broadcast(&item_cv[item_id], &mtx);
        }
        smutex_unlock(&mtx);
    } else {
        smutex_lock(&item_mtx[item_id]);
        Item &it = inventory[item_id];
        if (it.valid) {
            bool increased = (discount > it.discount);
            it.discount = discount;
            if (increased) scond_broadcast(&item_cv_fine[item_id], &item_mtx[item_id]);
        }
        smutex_unlock(&item_mtx[item_id]);
    }
}

/*
 * ------------------------------------------------------------------
 * setShippingCost --
 *
 *      Set the per-item shipping cost. If the shipping cost
 *      decreased, wake any waiters.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
setShippingCost(double cost)
{
    if (!fineMode) {
        smutex_lock(&mtx);
        bool decreased = (cost < shippingCost);
        shippingCost = cost;
        if (decreased) {
            for (int i = 0; i < INVENTORY_SIZE; ++i) scond_broadcast(&item_cv[i], &mtx);
        }
        smutex_unlock(&mtx);
    } else {
        smutex_lock(&global_mtx);
        bool decreased = (cost < shippingCost);
        shippingCost = cost;
        smutex_unlock(&global_mtx);

        if (decreased) {
            // Wake per-item waiters; each CV must be signaled with its own lock
            for (int i = 0; i < INVENTORY_SIZE; ++i) {
                smutex_lock(&item_mtx[i]);
                scond_broadcast(&item_cv_fine[i], &item_mtx[i]);
                smutex_unlock(&item_mtx[i]);
            }
        }
    }
    smutex_unlock(&mtx);
}

/*
 * ------------------------------------------------------------------
 * setStoreDiscount --
 *
 *      Set the store discount. If the discount increased, wake any
 *      waiters.
 *
 * Results:
 *      None.
 *
 * ------------------------------------------------------------------
 */
void EStore::
setStoreDiscount(double discount)
{
    if (!fineMode) {
        smutex_lock(&mtx);
        bool increased = (discount > storeDiscount);
        storeDiscount = discount;
        if (increased) {
            for (int i = 0; i < INVENTORY_SIZE; ++i) scond_broadcast(&item_cv[i], &mtx);
        }
        smutex_unlock(&mtx);
    } else {
        smutex_lock(&global_mtx);
        bool increased = (discount > storeDiscount);
        storeDiscount = discount;
        smutex_unlock(&global_mtx);

        if (increased) {
            for (int i = 0; i < INVENTORY_SIZE; ++i) {
                smutex_lock(&item_mtx[i]);
                scond_broadcast(&item_cv_fine[i], &item_mtx[i]);
                smutex_unlock(&item_mtx[i]);
            }
        }
    }
    smutex_unlock(&mtx);
}
/*
 * ------------------------------------------------------------------
 * getItemQuantity --
 *
 *      Return the quantity of the specified item in the store.
 *
 * Results:
 *      The quantity of the item in the store.
 *
 * ------------------------------------------------------------------
 */
int EStore::
getItemQuantity(int item_id)
{
    if (item_id < 0 || item_id >= INVENTORY_SIZE) return 0;

    if (!fineMode) {
        smutex_lock(&mtx);
        int q = (inventory[item_id].valid ? inventory[item_id].quantity : 0);
        smutex_unlock(&mtx);
        return q;
    } else {
        smutex_lock(&item_mtx[item_id]);
        int q = (inventory[item_id].valid ? inventory[item_id].quantity : 0);
        smutex_unlock(&item_mtx[item_id]);
        return q;
    }
}
