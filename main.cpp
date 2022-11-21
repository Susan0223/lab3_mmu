#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <cstring>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <deque>
#include <climits>

#define max_page_num 64
using namespace std;

/***********************************/
// class Definition
/***********************************/

typedef struct VMA{
public:
    int start_vpage;
    int end_vpage;
    bool write_protected;
    bool file_mapped;
} VAM;

typedef struct pte_t {
    unsigned int VALID : 1;
    unsigned int REFERENCED : 1;
    unsigned int MODIFIED : 1;
    unsigned int WRITE_PROTECT : 1;
    unsigned int PAGEDOUT : 1;
    unsigned int frame_number : 7;
    // 20 free bits
    unsigned int FILEMAPPED : 1;
} pte_t;

typedef struct frame_t{
    int pid;
    int frame_id;
    int vpage;
    bool dirty;
} frame_t;

class Process{
public:
    int pid;
    vector<VMA> vma_vector;
    vector<pte_t> page_table;
    Process(): page_table(max_page_num){};

};

/***********************************/
// global variable
/***********************************/

int frame_number = 16;

vector<frame_t> frame_table;
deque<frame_t> free_pool;
vector<Process*> proc_vector;
Process* proc;
vector<pair<string, int>> instruction_list;

/***********************************/
// Pagers
/***********************************/

class Pager {
public:
    virtual frame_t* select_victim_frame() = 0;
};

class FIFO : public Pager{
public:
    int victim_frame_index;
    frame_t* select_victim_frame(){
        if(victim_frame_index == frame_table.size()){
            victim_frame_index = 0;
            return &frame_table.at(victim_frame_index);
        }
        frame_t* frame = &frame_table.at(victim_frame_index);
        victim_frame_index += 1;
        return frame;
    }
};

/***********************************/
// methods
/***********************************/
//frame_t *get_frame() {
//    frame_t *frame = allocate_frame_from_free_list();
//    if (frame == NULL) frame = THE_PAGER->select_victim_frame();
//    return frame;
//}

bool not_sevg(Process* p, int vpage){
    int sevg_flag = false;
    for(int i = 0; i < p->vma_vector.size(); i++){
        if(vpage >= p->vma_vector[i].start_vpage && vpage <= p->vma_vector[i].end_vpage) {
            sevg_flag = true;
        }
    }
    return sevg_flag;
}

void readFile(char* inputPtr){

    int total_proc = -1;
    int curr_proc_id = 0;
    int vma_num = 0;
    int vma_count = 0;
    bool is_new_proc = true;
    string line;
    string fileName(inputPtr);
    ifstream inputFile(fileName);

    if(!inputFile.is_open()){
        cout << "Cannot Open File" << endl;
        exit(1);
    }
    while(getline(inputFile, line)){

        if(line[0] == '#') continue;
        char* c = const_cast<char*>(line.c_str());
        char* token = strtok(c, " \t\n");

        // get number of processes
        if(total_proc == -1){
            total_proc = stoi(token);
            continue;
        }

        // read processes
        if(curr_proc_id < total_proc){
            vector<char*> temp;

            // 1) create new process 2) get total vma of new process
            if(is_new_proc){
                Process *p = new Process();
                p->pid = curr_proc_id;
                proc_vector.push_back(p);
                vma_num = stoi(token);
                vma_count = 0;
                is_new_proc = false;
                continue;
            }
            //read vma details
            else{
                while(token){
                    temp.push_back(token);
                    token = strtok(NULL, " \t\n");
                }
                VMA vma;
                vma.start_vpage = stoi(temp.at(0));
                vma.end_vpage = stoi(temp.at(1));
                vma.write_protected = stoi(temp.at(2));
                vma.file_mapped = stoi(temp.at(3));
                proc_vector.back()->vma_vector.push_back(vma);
                vma_count += 1;
                if(vma_num == vma_count){
                    is_new_proc = true;
                    curr_proc_id += 1;
                }
            }
        }
        //read instructions
        else{
            pair<string, int> p;
            p.first = string(token);
            token = strtok(NULL, " \t\n");
            p.second = stoi(token);
            //cout << "instruction " + p.first + " " + to_string(p.second) << endl;
            instruction_list.push_back(p);
        }
    }
    inputFile.close();
}
//void handle_input(){
//
//    int proc_num = stoi(input_list.at(0));
//    cout << proc_num << endl;
//}

void simulation(){

//    for(int i = 0; i < frame_number; i++){
//        frame_t frame;
//        frame.pid = -1;
//        frame.dirty = false;
//        frame.vpage = -1;
//        frame.frame_id = i;
//        frame_table.push_back(frame);
//        //free_pool.push_back(frame);
//    }

    /***********************************/
    //init
    VMA vma;
    vma.start_vpage = 0;
    vma.end_vpage = 63;
    vma.file_mapped = false;
    vma.write_protected = false;
    //free_frame.push_back(1);
    string operation = "r";
    int vpage = 0;
    proc = new Process();
    Process* proc_t = proc;
    proc_t->pid = 0;
    vector<VMA> VMAs;
    VMAs.push_back(vma);
    proc_t->vma_vector = VMAs;
    proc_vector.push_back(proc_t);
    Pager* THE_PAGER = new FIFO();

    /***********************************/
    //context switch
    if(operation == "c"){
        proc_t->pid = 0;
        proc_t->vma_vector.push_back(vma);
        printf("%d: ==> c %d\n", proc->pid, proc->pid);
    }
    if(operation == "r"){

        pte_t* pte = &(proc->page_table[vpage]);

        if(!(pte->VALID)){
            if(not_sevg(proc, vpage) == false) {
                cout << "SEVG" << endl;
            }
        }
        frame_t* victim_frame;
        if(free_pool.size() != 0){
            frame_t frame = free_pool.front();
            free_pool.pop_front();
            victim_frame = &(frame);
        }else{
            victim_frame = THE_PAGER->select_victim_frame();
            // check dirty
            if(victim_frame->dirty == true){
                cout << " UNMAP " << victim_frame->pid << ":" << victim_frame->vpage << endl;
            }
            // check modified / file mapped
            int prev_proc_id = victim_frame->pid;
            pte_t* prev_pte = &proc_vector.at(prev_proc_id)->page_table[victim_frame->vpage];
            if(prev_pte->MODIFIED && prev_pte->FILEMAPPED){
                cout << "FOUT" << endl;
            }
            else if(prev_pte->MODIFIED){
                prev_pte->PAGEDOUT = 1;
                cout << "OUT" << endl;
            }
            //now victim_frame is available -> reset prev pte

            prev_pte->VALID = 0;
            prev_pte->MODIFIED = 0;
            prev_pte->REFERENCED = 0;
            prev_pte->frame_number = 0;
        }
        // reset curr pte and frame
        victim_frame->vpage = vpage;
        victim_frame->pid = proc->pid;
        pte->frame_number = victim_frame->frame_id;
        pte->VALID = 1;
        if(pte->FILEMAPPED) {
            cout << "FIN" << endl;
        }
        else if(pte->PAGEDOUT){
            cout << "IN" << endl;
        }
        else{
            cout << "ZERO" << endl;
        }
        cout << "MAP " + to_string(victim_frame->frame_id) << endl;
    }
}

int main(int argc, char *argv[]){

    readFile(argv[1]);
    for(int i = 0; i < proc_vector.size(); i++){
        cout << "proc id:" + to_string(proc_vector.at(i)->pid) + " vma size " + to_string(proc_vector.at(i)->vma_vector.size()) << endl;
    }
    cout << "instruction list size " + to_string(instruction_list.size()) << endl;
    //handle_input();
    //simulation();
    //read file
        //create process vector
            //virtual memory addresses
        //create frame_table
        //create free pool
    //determine algorithm
    //simulation
        //context switch
        //read
            //segmentation fault exception
        //write
            //segmentation fault exception
            //check free pool -> paging
                //find victim - Pager Class
                //unmap the victim = removed from page_table_entry
                //save frame to disk
                //fill frame with current instruction address space
                //map it
            //mark M and R bit
        //exit


}