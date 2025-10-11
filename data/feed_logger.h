//#pragma once

#ifndef FEED_LOGGER_H
#define FEED_LOGGER_H
#include "types.h" // Menggunakan struct dari types.h



namespace FeedLogger {
    /**
     * @brief Menambahkan data feed ke file log CSV.
     * * @param q Data LiveQuote yang akan di-log.
     * @param useThrottling Jika true, log hanya akan ditulis jika harga berubah 
     * atau jika sudah melewati interval waktu tertentu.
     * Jika false, semua data akan ditulis.
     */
    void append(const LiveQuote& q, bool useThrottling = false);
}

#endif  //FEED_LOGGER_H
