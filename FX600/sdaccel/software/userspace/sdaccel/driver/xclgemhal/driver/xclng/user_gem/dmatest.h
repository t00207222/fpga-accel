/**
 * Copyright (C) 2016-2018 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#ifndef DMATEST_H
#define DMATEST_H

#include <chrono>
#include <future>
#include <vector>
#include <cstring>
#include <iostream>

#include "driver/include/xclhal2.h"

namespace xcldev {
    class Timer {
        std::chrono::high_resolution_clock::time_point mTimeStart;
    public:
        Timer() {
            reset();
        }
        long long stop() {
            std::chrono::high_resolution_clock::time_point timeEnd = std::chrono::high_resolution_clock::now();
            return std::chrono::duration_cast<std::chrono::microseconds>(timeEnd - mTimeStart).count();
        }
        void reset() {
            mTimeStart = std::chrono::high_resolution_clock::now();
        }
    };

    class DMARunner {
        std::vector<unsigned int> mBOList;
        xclDeviceHandle mHandle;
        size_t mSize;
        unsigned mFlags;

        int runSyncWorker(std::vector<unsigned>::const_iterator b,
                          std::vector<unsigned>::const_iterator e,
                          xclBOSyncDirection dir) {
            int result = 0;
            while (b < e) {
                result = xclSyncBO(mHandle, *b, dir, mSize, 0);
                if (result != 0)
                    break;
                ++b;
            }
            return result;
        }

        int runSync(xclBOSyncDirection dir, bool mt) {
            const std::vector<unsigned>::const_iterator b = mBOList.begin();
            const std::vector<unsigned>::const_iterator e = mBOList.end();
            if (mt) {
                auto len = e - b;
                auto mid = b + len/2;
                auto future0 = std::async(std::launch::async, &DMARunner::runSyncWorker, this, b, mid, dir);
                auto future1 = std::async(std::launch::async, &DMARunner::runSyncWorker, this, mid, e, dir);
                return (future0.get() + future1.get());
            }
            else {
                auto future0 = std::async(std::launch::async, &DMARunner::runSyncWorker, this, b, e, dir);
                return future0.get();
            }
        }

    public:
        DMARunner(xclDeviceHandle handle, size_t size, unsigned flags) : mHandle(handle), mSize(size), mFlags(flags) {
            long long count = 0x100000000/size;
            if (count > 0x40000)
                count = 0x40000;

            for (long long i = 0; i < count; i++) {
                unsigned bo = xclAllocBO(mHandle, mSize, XCL_BO_DEVICE_RAM, mFlags);
                if (bo == 0xffffffff)
                    break;
                mBOList.push_back(bo);
            }
        }

        ~DMARunner() {
            for (auto i : mBOList)
                xclFreeBO(mHandle, i);
        }

        int run() {
            char *buf = new char[mSize + 1];
            std::memset(buf, 'x', mSize);

            int result = 0;
            for (auto i : mBOList)
                result += xclWriteBO(mHandle, i, buf, mSize, 0);

            if (result)
            {
                delete [] buf;
                return result;
            }

            Timer timer;
            result = runSync(XCL_BO_SYNC_BO_TO_DEVICE, true);
            double timer_stop = timer.stop();
            double rate = (mBOList.size() * mSize)/0x100000; // MB
            rate /= timer_stop;
            rate *= 1000000; // s
            std::cout << "INFO: Host -> PCIe -> MIG write bandwidth = " << rate << " MB/s\n";

            //Clear out the host buffer
            std::memset(buf, 0, mSize);
            timer.reset();
            result += runSync(XCL_BO_SYNC_BO_FROM_DEVICE, true);
            timer_stop = timer.stop();
            rate = (mBOList.size() * mSize)/0x100000; // MB
            rate /= timer_stop;
            rate *= 1000000; //
            std::cout << "INFO: Host <- PCIe <- MIG read bandwidth = " << rate << " MB/s\n";
            /*
             * Checking data validity on whole 16GB of DDR will be very slow
             * Instead checking data validity on smaller reads of upto 128 bytes in main.cpp
            for (unsigned bo : mBOList) {
                //Check content of each BO
                if( std::string((char*)buf) != std::string((char*)bufPattern)) { // strings are equal
                  std::cout << "Error: read data didn't meet the pattern  \n";
                  std::cout << "Error: read data is: " << std::string((char*)buf) << std::endl;
                }
            }
            */

            delete [] buf;
            return result;
        }
    };
}

#endif /* DMATEST_H */
// 67d7842dbbe25473c3c32b93c0da8047785f30d78e8a024de1b57352245f9689
