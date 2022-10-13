#include <cstdio>
#include <iostream>
#include <map>
#include <chrono>
#include <string>
#include <bits/stdc++.h>
#include <ctime>
#include <cstdint>

using namespace std;

extern "C"{
    #include "fake_receiver.h"
}

//const for starting end ending session
const string START_1 = "0A0#6601";
const string START_2 = "0A0#FF01";
const string STOP_1 = "0A0#66FF";

//const for id and payload size
const int MAX_SIZE_ID = 3;
const int MAX_SIZE_PAYLOAD = 16;

//these are the possible states
enum STATUS{IDLE, START};

//counter for csv files
int counter_csv_files = 1;

/**
 * create a unique string to use it as name for a file using date and time system
 * @return the string created
 */
string createNameFile();

/**
 * check if the message read is a start message
 * @param line the line read
 * @return true if is a start message, false otherwise
 */
bool check_start_message(string line);

/**
 * check if the message read is a stop message
 * @param line the line read
 * @return true if is a stop message, false otherwise
 */
bool check_stop_message(string line);

/**
 * parse the id message from exa base to deca base
 * @param line the message read
 * @return the deca value of the message in 12bit, -1 if some errors occur (eg. not even payload chars)
 */
int16_t parseId(string line);

/**
 * parse the payload message from exa base to deca base
 * @param line the message read
 * @return the deca value of the message in 64bit, -1 if some errors occur (eg. not even payload chars)
 */
int64_t parsePayload(string line);

/**
 * get the substring id from the message
 * @param line the message red
 * @return the id
 */
string getId(string line);

/**
 * get the substring payload from the message
 * @param line the message red
 * @return the payload
 */
string getPayload(string line);

/**
 * convert the exa string to decimal base value
 * @param hexVal the string
 * @return the decimal value in 64 bit
 */
int64_t hexadecimalToDecimal(string hexVal);

struct values;
/**
 * create a csv file and save it in bin folder with name file_name
 * @param messages the map containing the values
 * @param file_name the name to give to the file
 * @return return 1 if some errors occurs
 */
int createCSV(map<string, values *> messages, string file_name);
/**
 * file log which saves every data with millis in located in
 * \bin\logs\
 *
 * file csv which saves the data from start sessions located in
 * \bin\csv\
 *
 * the file used for starting the start interface is candump.log, located in
 * \bin\
 */
int main() {
    using std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::duration;
    using std::chrono::milliseconds;

    //file for log
    string file_name = createNameFile();
    ofstream log;
    log.open("logs\\"+file_name+".log");

    //error opening log file
    if (log.fail()) {
        cout << "Error creating output file";
        return 1;
    }

    //the program start at IDLE state
    STATUS state = IDLE;

    //struct to contain values about message
    typedef struct{
        int nMsg;
        double mean_time;
        double last_time;//indicates the last time to get a msg
    } values;

    //map containing stats about the id message
    map <string, values *> messages;

    //opening CAN interface
    int status = open_can("candump.log");
    int count = 0;
    //error in opening file
    if(status == -1){
        cout << "Error opening CAN interface";
        return 1;
    }

    //variable to count the time between every message
    auto t1 = high_resolution_clock::now();
    string logMsg;

    //reading messages
    int nByte;//numbers of byte read
    char message[MAX_CAN_MESSAGE_SIZE];//the message read

    do{
        nByte = can_receive(message);
        auto t2 = high_resolution_clock::now();//time after the message

        //TODO bug, printing strange char, problem of receiver at 4 iteraction??
        //calculate the time elapsed from the beginning
        //if no byte read then don't print anything on file
        if(nByte != -1) {
            duration<double, std::milli> ms_double = (t2 - t1);//to get value, ms_double.count()
            //write on log file the millis and the message received
            logMsg.append(to_string(ms_double.count()));
            logMsg.append(" ");
            logMsg.append(message);
            logMsg.append("\n");
            log << logMsg;
            logMsg.clear();
        }

        //check if the message is a start or a stop message
        if(check_start_message(message)){
            //change from IDLE to START if not already in START
            if(state == IDLE) {
                state = START;
                cout << "starting START" << endl;
            }

        } else if(check_stop_message(message)){
            //change from START to IDLE
            state = IDLE;

            //TODO export csv file of the map using a method
            //createCSV(messages, file_name);
            fstream csv;
            string path = "csv\\";
            path.append(file_name);
            path.append("_");
            path.append(to_string(counter_csv_files));
            counter_csv_files++;//update che number of csv files created
            csv.open(path+".csv", ios::out);

            if(csv.fail()){
                cout << "Error creating csv file" << endl;
                return 1;
            }

            for (const auto &message: messages)
                csv << message.first << "," << message.second->nMsg << "," << message.second->mean_time << "\n";

            csv.close();
            messages.clear();//remove all data collected

             //TODO remove this print below
            cout << "Finish of START status" << endl;
            //end START status
        }

        //if in start, saving values in map
        if(state == START) {
            //search for an existing id
            string id, payload;

            id = getId(message);
            //payload = getPayload(message);

            //save the iterator of the search
            auto iterator = messages.find(id);

            //if there, increment counter and update mean
            //else, create new entry for key = ID
            if (iterator == messages.end()) {
                //create new struct for the current id
                values *v = new values;
                v->nMsg = 1;

                duration<double, std::milli> ms_double = (t2 - t1);
                v->mean_time = ms_double.count();
                v->last_time = ms_double.count();

                messages.insert(pair<string, values *>(id,v));
            } else {
                iterator->second->nMsg++;//update numbers of message with that id

                //update last_time with the last one read
                duration<double, std::milli> ms_double = (t2 - t1);
                double last_time = ms_double.count();
                iterator->second->last_time = last_time;

                //update mean
                double mean = iterator->second->mean_time;
                mean = (mean + last_time) / iterator->second->nMsg;
                iterator->second->mean_time = mean;
            }

        }

        //TODO remove print debug msg
        cout << "nByte: " << nByte << endl;
        cout << "message: " << message << endl;
    }while(nByte != -1);//error in reading or eof

    //close log file
    log.close();

    //closing CAN interface
    close_can();

    return 0;
}

string createNameFile(){
    time_t rawtime;
    struct tm * timeinfo;
    char buffer[80];
    time (&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer,80,"%d-%m-%Y %H-%M-%S",timeinfo);
    return string(buffer);
}

bool check_start_message(string line){
    if(line.compare(START_1) == 0 || line.compare(START_2) == 0){
        //read a start message
        return true;
    } else
        return false;
}

bool check_stop_message(string line){
    if(line.compare(STOP_1) == 0){
        //read a stop message
        return true;
    } else
        return false;
}

int16_t parseId(string line){
    //split the message in two parts, id and payload
    string id = getId(line);

    if(id.size()<=MAX_SIZE_ID){
        //everything is ok
        return hexadecimalToDecimal(id);
    } else {
        //error of syntax
        return -1;
    }
}

int64_t parsePayload(string line){
    string payload = getPayload(line);

    if(payload.size()<=MAX_SIZE_PAYLOAD && payload.size()%2==0){
        //everything ok
        return hexadecimalToDecimal(payload);
    } else {
        //error in syntax
        return -1;
    }
}


int64_t hexadecimalToDecimal(string hexVal){
    int len = hexVal.size();
    int base = 1;
    int64_t dec_val = 0;

    for (int i = len - 1; i >= 0; i--) {

        if (hexVal[i] >= '0' && hexVal[i] <= '9') {
            dec_val += (int(hexVal[i]) - 48) * base;

            base = base * 16;
        }

        else if (hexVal[i] >= 'A' && hexVal[i] <= 'F') {
            dec_val += (int(hexVal[i]) - 55) * base;
            base = base * 16;
        }
    }
    return dec_val;
}

string getId(string line){
    string sub;

    for (int i = 0; line[i] != '#'; i++)
        sub.push_back(line[i]);


    return sub;
}

string getPayload(string line){
    string sub;
    int index = 0;

    for(int index=0; line[index]!='#'; index++){
        //do nothing, ignore the chars to get the payload
    }

    for(index; index<line.size(); index++){
        sub.push_back(line[index]);
    }

    return sub;
}

int createCSV(map<string, values *> messages, string file_name){
    fstream csv;
    string path = "csv\\";
    path.append(file_name);
    path.append("_");
    path.append(to_string(counter_csv_files));
    counter_csv_files++;//update che number of csv files created
    csv.open(path+".csv", ios::out);

    if(csv.fail()){
        cout << "Error creating csv file" << endl;
        return 1;
    }

    for (const auto &message: messages)
        csv << message.first << "," << message.second->nMsg << "," << message.second->mean_time << "\n";

    csv.close();
    messages.clear();//remove all data collected

    return 0;
}