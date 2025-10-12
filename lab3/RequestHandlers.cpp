#include <cstdio>
#include "RequestHandlers.h"
#include "Request.h"
#include "EStore.h"
#include "sthread.h"

void add_item_handler(void *args) {
    auto *req = static_cast<AddItemReq*>(args);
    // item_id, quantity, price, discount
    printf("Handling AddItemReq: item_id - %d, quantity - %d, price - $%.2f, discount - %.2f\n",
           req->item_id, req->quantity, req->price, req->discount);
    req->store->addItem(req->item_id, req->quantity, req->price, req->discount);
    delete req;
}

void remove_item_handler(void *args) {
    auto *req = static_cast<RemoveItemReq*>(args);
    printf("Handling RemoveItemReq: item_id - %d\n", req->item_id);
    req->store->removeItem(req->item_id);
    delete req;
}

void add_stock_handler(void *args) {
    auto *req = static_cast<AddStockReq*>(args);
    printf("Handling AddStockReq: item_id - %d, additional_stock - %d\n",
           req->item_id, req->additional_stock);
    req->store->addStock(req->item_id, req->additional_stock);
    delete req;
}

void change_item_price_handler(void *args) {
    auto *req = static_cast<ChangeItemPriceReq*>(args);
    printf("Handling ChangeItemPriceReq: item_id - %d, new_price - $%.2f\n",
           req->item_id, req->new_price);
    req->store->priceItem(req->item_id, req->new_price);
    delete req;
}

void change_item_discount_handler(void *args) {
    auto *req = static_cast<ChangeItemDiscountReq*>(args);
    printf("Handling ChangeItemDiscountReq: item_id - %d, new_discount - %.2f\n",
           req->item_id, req->new_discount);
    req->store->discountItem(req->item_id, req->new_discount);
    delete req;
}

void set_shipping_cost_handler(void *args) {
    auto *req = static_cast<SetShippingCostReq*>(args);
    printf("Handling ShippingCostReq: new shipping cost - $%.2f\n", req->new_cost);
    req->store->setShippingCost(req->new_cost);
    delete req;
}

void set_store_discount_handler(void *args) {
    auto *req = static_cast<SetStoreDiscountReq*>(args);
    printf("Handling SetStoreDiscountReq: new_discount - %.2f\n", req->new_discount);
    req->store->setStoreDiscount(req->new_discount);
    delete req;
}

void buy_item_handler(void *args) {
    auto *req = static_cast<BuyItemReq*>(args);
    printf("Handling BuyItemReq: item_id - %d, budget - $%.2f\n",
           req->item_id, req->budget);
    req->store->buyItem(req->item_id, req->budget);
    delete req;
}

void buy_many_items_handler(void *args) {
    auto *req = static_cast<BuyManyItemsReq*>(args);
    // Print list size; printing all ids can be noisy
    printf("Handling BuyManyItemsReq: items - %zu, budget - $%.2f\n",
           req->item_ids.size(), req->budget);
    req->store->buyManyItems(&req->item_ids, req->budget);
    delete req;
}

void stop_handler(void* args) {
    (void)args;
    printf("Handling StopHandlerReq: Quitting.\n");
    // Terminate the worker thread
    sthread_exit();
}
