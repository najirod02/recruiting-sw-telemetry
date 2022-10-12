#include <stdio.h>
#include <iostream>
#include <map>
#include <chrono>
#include <string>
#include <bits/stdc++.h>

using namespace std;

extern "C"{
    #include "fake_receiver.h"
}

//const for starting end ending session
const string START_1 = "0A0#6601";
const string START_2 = "0A0#FF01";
const string STOP_1 = "0A0#66FF";

//these are the possible states
enum STATUS{IDLE, START};

bool check_start_message(string line);

bool check_stop_message(string line);

int main() {
    using std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::duration;
    using std::chrono::milliseconds;

    //file for log
   ofstream log;
   log.open("out.log");

    //error opening log file
    if (log.fail()) {
        cout << "Error opening out.log file";
        return 1;
    }

    //the program start ad IDLE state
    STATUS state = IDLE;

    //struct to contain values about message
    struct values{
        int nMsg;
        double mean_time;
    };

    //map containing stats about the id message
    map <string, values> messages;

    //opening CAN interface
    //TODO change path to read file
    int status = open_can("C:/Users/dorij/OneDrive/Desktop/eagletrt/recruiting-sw-telemetry/candump.log");

    //reading messages
    int nByte;//numbers of byte read
    char message[MAX_CAN_MESSAGE_SIZE];//the message read

    //error in opening file
    if(status == -1){
        cout << "Error opening CAN interface";
        return 1;
    }

    //variable to count the time between every message
    auto t1 = high_resolution_clock::now();
    string logMsg = "";

    do{
        nByte = can_receive(message);
        auto t2 = high_resolution_clock::now();//time after the message

        //calculate the time elapsed from the begining
        /* Getting number of milliseconds as a double. */
        duration<double, std::milli> ms_double = (t2 - t1);//to get value, ms_double.count()

        //write on log file the millis and the message received
        logMsg.append(to_string(ms_double.count()));
        logMsg.append(" ");
        logMsg.append(message);
        log << logMsg;


        //check if the message is a start or a stop message
        if(check_start_message(message)){
            //change from IDLE to START if not already in START
            if(state == IDLE)
                state = START;
        } else if(check_stop_message(message)){
            //change from START to IDLE
            state = IDLE;
        }




        cout << "nByte: " << nByte << endl;
        cout << "message: " << message << endl;

    }while(nByte != -1);//error in reading or eof

    //close log file
    log.close();

    //closing CAN interface
    close_can();
    return 0;
}

/**
 * check if the message read is a start message
 * @param line the line read
 * @return true if is a start message, false otherwise
 */
bool check_start_message(string line){
    if(line.compare(START_1) == 0 || line.compare(START_2) == 0){
        //read a start message
        return true;
    } else
        return false;
}

/**
 * check if the message read is a stop message
 * @param line the line read
 * @return true if is a stop message, false otherwise
 */
bool check_stop_message(string line){
    if(line.compare(STOP_1) == 0){
        //read a stop message
        return true;
    } else
        return false;
}