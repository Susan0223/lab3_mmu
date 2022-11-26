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

/*********************************** struct and class *************************/

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
    unsigned int CONFIGURATED : 1;

} pte_t;
typedef struct frame_t{
    int pid;
    int frame_id;
    int vpage;
    bool dirty;
} frame_t;
typedef struct summary_t{
    int sevg_count;
    int segprot_count;
    int unmap_count;
    int map_count;
    int pageins_count;
    int pageouts_count;
    int zero_count;
} summary_t;
class Process{
public:
    int pid;
    summary_t summary;
    vector<VMA> vma_vector;
    vector<pte_t> page_table;
    Process(): page_table(max_page_num){};

};

/****************************** global variable ******************************/

int frame_size = 16;
int victim_frame_index = 0;
Process* curr_proc;
deque<frame_t> frame_table;
deque<frame_t*> victim_table;
deque<frame_t*> free_pool;
vector<Process*> proc_vector;
Process* proc;
vector<pair<string, int>> instruction_list;

/****************************** summary variable *****************************/

unsigned long long inst_count;
unsigned long long proc_exit_count;
unsigned long long context_switch_count;

/*********************************** Pagers ***********************************/
class Pager {
public:
    virtual frame_t* select_victim_frame() = 0;
};
class FIFO : public Pager{
public:
    frame_t* select_victim_frame(){
        if(victim_frame_index == frame_size){
            victim_frame_index = 0;
        }
        frame_t* frame = victim_table.at(victim_frame_index);
        victim_frame_index += 1;
        return frame;
    }
};

/*********************************** methods ***********************************/
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
        inst_count = instruction_list.size();
    }
    inputFile.close();
}
void initialize_frame_table(int frame_size){
    for(int i = 0; i < frame_size; i++){
        frame_t frame;
        frame.pid = -1;
        frame.dirty = false;
        frame.vpage = -1;
        frame.frame_id = i;
        frame_table.push_back(frame);
    }
}
void initialize_free_pool(){
    for(int i = 0; i < frame_table.size(); i++){
        frame_t* frame = &frame_table.at(i);
        free_pool.push_back(frame);
        //cout << frame->frame_id << endl;
    }
}
void configurate_pte(int pid, int vpage){
    Process* proc = proc_vector.at(pid);
    pte_t* pte = &proc->page_table.at(vpage);
    for(int i = 0; i < proc->vma_vector.size(); i++){
        VMA* vma = &proc->vma_vector.at(i);
        if(vpage >= vma->start_vpage && vpage <= vma->end_vpage){
            pte->FILEMAPPED = vma->file_mapped;
            pte->WRITE_PROTECT = vma->write_protected;
        }
    }
    pte->CONFIGURATED = 1;
}
void reset_frame_queue(){
    while(!victim_table.empty()){
        victim_table.pop_back();
    }
    initialize_free_pool();
}
void simulation(){

    initialize_frame_table(frame_size);
    Pager* THE_PAGER = new FIFO();
    initialize_free_pool();

/*******************************************************************************/

    //cout << "finish initialization" << endl;
    for(int i = 0; i < instruction_list.size(); i++) {
        //cout << "instruction " + to_string(i) << endl;
        string operation = instruction_list.at(i).first;
        int second = instruction_list.at(i).second;

        //context switch
        if (operation == "c") {
            context_switch_count += 1;
            curr_proc = proc_vector.at(second);
            printf("%d: ==> c %d\n", i, curr_proc->pid);
        }
        //read and write
        if (operation == "r" || operation == "w") {

            int vpage = second;
            printf("%d: ==> %s %d\n", i, operation.c_str(), vpage);
            pte_t *pte = &(curr_proc->page_table[vpage]);

            if (!(pte->CONFIGURATED)) {
                configurate_pte(curr_proc->pid, vpage);
            }

            if (!(pte->VALID)) {
                if (not_sevg(curr_proc, vpage) == false) {
                    curr_proc->summary.sevg_count +=1;
                    cout << " SEVG" << endl;
                    continue;
                }

                frame_t *victim_frame;

                //choose from free_pool
                if (free_pool.size() != 0) {
                    frame_t* free_frame = free_pool.front();
                    free_frame->pid = curr_proc->pid;
                    free_frame->vpage = vpage;
                    pte->frame_number = free_frame->frame_id;
                    victim_frame = free_frame;
                    victim_table.push_back(victim_frame);
                    free_pool.pop_front();
                }

                //choose with algo
                else {

                    victim_frame = THE_PAGER->select_victim_frame();
                    cout << " UNMAP " << victim_frame->pid << ":" << victim_frame->vpage << endl;

                    // check modified and file mapped
                    int prev_proc_id = victim_frame->pid;
                    pte_t *prev_pte = &proc_vector.at(prev_proc_id)->page_table[victim_frame->vpage];
                    if (prev_pte->MODIFIED && prev_pte->FILEMAPPED) {
                        cout << " FOUT" << endl;
                    } else if (prev_pte->MODIFIED) {
                        prev_pte->PAGEDOUT = 1;
                        cout << " OUT" << endl;
                    }

                    //now victim_frame is available -> reset prev pte
                    prev_pte->VALID = 0;
                    prev_pte->MODIFIED = 0;
                    prev_pte->REFERENCED = 0;
                    prev_pte->frame_number = 0;
                }

                // reset curr pte and frame
                victim_frame->vpage = vpage;
                victim_frame->pid = curr_proc->pid;
                pte->VALID = 1;

                if (pte->FILEMAPPED) {
                    cout << " FIN" << endl;
                } else if (pte->PAGEDOUT) {
                    cout << " IN" << endl;
                } else {
                    cout << " ZERO" << endl;
                }
                cout << " MAP " + to_string(victim_frame->frame_id) << endl;

                if(pte->WRITE_PROTECT && operation == "w"){
                    curr_proc->summary.segprot_count += 1;
                    cout << " SEGPROT" << endl;
                }else if(operation == "w"){
                    pte->MODIFIED = 1;
                }
            }
        }
        //exit
        if (operation == "e"){

            proc_exit_count += 1;
            printf("EXIT current process %d\n", curr_proc->pid);

            // check page table and reset flags
            for(int i = 0; i < curr_proc->page_table.size(); i++){
                pte_t* pte = &curr_proc->page_table.at(i);
                summary_t summary = curr_proc->summary;
                frame_t* frame = &frame_table.at(pte->frame_number);
                if(pte->VALID){
                    printf(" unmap %d:%d", curr_proc->pid, frame->vpage);
                    summary.unmap_count += 1;
                    frame->pid = -1;
                    frame->vpage = -1;
                    frame->dirty = false;
                }
                pte->VALID = 0;
                pte->frame_number = -1;
                pte->MODIFIED = 0;
                pte->WRITE_PROTECT = 0;
                pte->CONFIGURATED = 0;
                pte->PAGEDOUT = 0;
                pte->REFERENCED = 0;
                pte->FILEMAPPED = 0;
            }

            reset_frame_queue();
        }
    }
}

int main(int argc, char *argv[]){
    readFile(argv[1]);
    simulation();
}