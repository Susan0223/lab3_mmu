#include <iostream>
#include <fstream>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define max_page_num 64
#define TAU 49
#define MAX_SIZE 30
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
    unsigned int counter : 32;
    unsigned long last_used_time;
} frame_t;
typedef struct summary_t{

    unsigned long unmaps;
    unsigned long maps;
    unsigned long ins;
    unsigned long outs;
    unsigned long fins;
    unsigned long fouts;
    unsigned long zeros;
    unsigned long segv;
    unsigned long segprot;

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

int frame_size;
int victim_frame_index = 0;
int instruction_count = 0;
int NRU_victim_index = 0;
int ofs = 0;
int* randvals;
int rand_num;
bool o_option = true;
bool page_table_option = true;
bool frame_table_option = true;
bool statistic_option = true;
string pager;
string options;
Process* curr_proc;
deque<frame_t> frame_table;
deque<frame_t*> victim_table;
deque<frame_t*> free_pool;
vector<Process*> proc_vector;
Process* proc;
vector<pair<string, int>> instruction_list;

/****************************** summary variable *****************************/

unsigned long long inst_count;
unsigned long long process_exits;
unsigned long long ctx_switches;
unsigned long long read_write;

/*********************************** Pagers ***********************************/
class Pager {
public:
    virtual frame_t* select_victim_frame() = 0;
    virtual void reset_counter(frame_t* victim_frame){};
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
    void reset_counter(frame_t* victim_frame){}
};
class CLOCK : public Pager{
public:
    frame_t* select_victim_frame(){

        frame_t* victim_frame;

        while(true){
            if(victim_frame_index == frame_size){
                victim_frame_index = 0;
            }
            victim_frame = victim_table.at(victim_frame_index);
            Process* p = proc_vector[victim_frame->pid];
            pte_t* pte = &p->page_table[victim_frame->vpage];
            if(pte->REFERENCED){
                pte->REFERENCED = 0;
                victim_frame_index +=1;
                continue;
            }else{
                pte->REFERENCED = 1;
                victim_frame_index +=1;
                break;
            }
        }
        return victim_frame;
    }
    void reset_counter(frame_t* victim_frame){}
};
class NRU : public Pager{
public:
    unsigned long last_referenced = 0;
    frame_t* select_victim_frame(){

        if (victim_frame_index >= frame_size) {
            victim_frame_index = 0;
        }

        bool update_reference = (instruction_count - last_referenced >= 50) ? true : false;

        frame_t* victim_frame = victim_table[victim_frame_index];
        Process* p = proc_vector[victim_frame->pid];
        pte_t* victim_pte = &p->page_table[victim_frame->vpage];
        int lowest_class = 2 * victim_pte->REFERENCED + victim_pte->MODIFIED;
        victim_frame_index += 1;

        if(lowest_class == 0 && !update_reference){
            victim_frame_index = ((victim_frame->frame_id == frame_size) ? 0 : victim_frame->frame_id + 1);
            return victim_frame;
        }

        for(int i = 0; i < frame_size; i++) {

            if (victim_frame_index >= frame_size) {
                victim_frame_index = 0;
            }
            frame_t *curr_frame = victim_table[victim_frame_index];
            p = proc_vector[curr_frame->pid];
            pte_t *curr_pte = &p->page_table[curr_frame->vpage];
            int curr_class = 2 * curr_pte->REFERENCED + curr_pte->MODIFIED;
            //int victim_pte_level = 2 * victim_pte->REFERENCED + victim_pte->MODIFIED;
            victim_frame_index += 1;

            if (curr_class == 0 && instruction_count < 50) {
                victim_frame = curr_frame;
                break;
            } else if (curr_class < lowest_class) {
                victim_pte = curr_pte;
                victim_frame = curr_frame;
                lowest_class = curr_class;
            }
        }

        //reset REFERENCE bit
        if(update_reference){
            //cout << "instruction count " + to_string(instruction_count)<< endl;
            for(int i = 0; i < frame_table.size(); i++){
                Process* p = proc_vector[frame_table[i].pid];
                pte_t* pte = &p->page_table[frame_table[i].vpage];
                pte->REFERENCED = 0;
            }
            last_referenced = instruction_count;
            //instruction_count = 0;
        }

        //increment victim_index by 1
        victim_frame_index = ((victim_frame->frame_id == frame_size) ? 0 : victim_frame->frame_id + 1);
        return victim_frame;
    }
    void reset_counter(frame_t* victim_frame){}
};
class AGING: public Pager{
public:
    void reset_counter(frame_t* victim_frame){
        victim_frame->counter = 0;
    }
    frame_t* select_victim_frame(){

        /* 1) right shift counter by 1
         * 2) add current REFERENCE bit to most left
         * 3) reset REFERENCE bit
         * 4) min_counter = min(min_counter, curr_counter)
         */
        if(victim_frame_index >= frame_size){
            victim_frame_index = 0;
        }
        frame_t* victim_frame = &frame_table[victim_frame_index];
        Process* p = proc_vector[victim_frame->pid];
        pte_t* victim_pte = &p->page_table[victim_frame->vpage];
        victim_frame->counter >>= 1;
        if(victim_pte->REFERENCED == 1){
            victim_frame->counter = (victim_frame->counter | 0x80000000);
            victim_pte->REFERENCED = 0;
        }
        victim_frame_index += 1;

        for(int i = 0; i < frame_size - 1; i++ ){

            if(victim_frame_index >= frame_size){
                victim_frame_index = 0;
            }
            frame_t* curr_frame = &frame_table[victim_frame_index];
            Process* curr_p = proc_vector[curr_frame->pid];
            pte_t* curr_pte = &curr_p->page_table[curr_frame->vpage];
            curr_frame->counter >>= 1;
            if(curr_pte->REFERENCED == 1) {
                curr_frame->counter = (curr_frame->counter | 0x80000000);
                curr_pte->REFERENCED = 0;
            }

            //cout << "victim_frame: " + to_string(victim_frame->frame_id) + " " + to_string(victim_frame->counter) << endl;
            //cout << "curr_frame: " + to_string(curr_frame->frame_id) + " " + to_string(curr_frame->counter) << endl;
            if(victim_frame->counter > curr_frame->counter){
                //cout <<"vicim_frame_id " + to_string(curr_frame->frame_id) << endl;
                //cout <<"min: " + to_string(min_counter) << endl;
                victim_frame = curr_frame;
            }

            victim_frame_index += 1;
        }
        victim_frame_index = (victim_frame->frame_id == frame_size) ? 0 : victim_frame->frame_id + 1;
        return victim_frame;
    }
};
class WS: public Pager{
public:
    frame_t* select_victim_frame(){

        unsigned long long min_time = ULLONG_MAX;
        frame_t* victim_frame = victim_table[victim_frame_index];

        for(int i = 0; i < frame_size; i++){

            if(victim_frame_index >= frame_size){
                victim_frame_index = 0;
            }

            frame_t* curr_frame = victim_table[victim_frame_index];
            Process* p = proc_vector[curr_frame->pid];
            pte_t* curr_pte = &p->page_table[curr_frame->vpage];

            unsigned long long frame_age = instruction_count - curr_frame->last_used_time;

            if(curr_pte->REFERENCED == 1){
                curr_pte->REFERENCED = 0;
                curr_frame->last_used_time = instruction_count;
            }else{
                if(frame_age > TAU) {
                    break;
                }
                else if(curr_frame->last_used_time < min_time){
                    victim_frame = curr_frame;
                    min_time = curr_frame->last_used_time;
                }
            }
            victim_frame_index += 1;
        }
        victim_frame_index = ((victim_frame->frame_id == frame_size) ? 0 : victim_frame->frame_id + 1);
        return victim_frame;
    }
    void reset_counter(frame_t* victim_frame){}
};
class RANDOM: public Pager{
public:
    frame_t* select_victim_frame(){
        if(ofs == rand_num){
            ofs = 0;
        }
        int random_index = randvals[ofs] % frame_size;
        frame_t* victim_frame = &frame_table[random_index];
        ofs++;
        return victim_frame;
    }
    void reset_counter(frame_t* victim_frame){}
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
void readFile(char* inputPtr, char* rfilePtr){

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

        char buff[MAX_SIZE];
        string rFileName(rfilePtr);
        ifstream rFile(rFileName);
        rFile.getline(buff, MAX_SIZE); // get the first line, that's '40000'
        int num = atoi(buff); // num: 40000
        rand_num = num;
        randvals = new int[num];
        for(int i = 0; i < num; i++) {  // get the rest 40000 numbers
            rFile.getline(buff, MAX_SIZE);
            randvals[i] = atoi(buff);
        }
        rFile.close();
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
        frame.counter = 0;
        frame_table.push_back(frame);
    }
}
void initialize_free_pool(){
    for(int i = 0; i < frame_table.size(); i++){
        frame_t* frame = &frame_table.at(i);
        free_pool.push_back(frame);
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
Pager* set_pager(string pager){
    if(pager[0] == 'f') return new FIFO();
    if(pager[0] == 'r') return new RANDOM();
    if(pager[0] == 'c') return new CLOCK();
    if(pager[0] == 'e') return new NRU();
    if(pager[0] == 'a') return new AGING();
    if(pager[0] == 'w') return new WS();
    return nullptr;
}
void print_output(){
    if(page_table_option){
        for(int i = 0; i < proc_vector.size(); i++){
            Process* p = proc_vector[i];
            printf("PT[%d]:", p->pid);
            for (int j = 0; j < max_page_num; j++){
                pte_t* pte = &p->page_table[j];
                if (!pte->VALID && pte->PAGEDOUT){
                    cout << " #";
                }else if(!pte->VALID){
                    cout << " *";
                }else {
                    string R = (pte->REFERENCED) ? "R" : "-";
                    string M = (pte->MODIFIED) ? "M" : "-";
                    string S = (pte->PAGEDOUT) ? "S" : "-";
                    cout << " " + to_string(j) + ":" + R+M+S;
                }
            }
            cout << endl;
        }
    }
    if(frame_table_option){
        cout << "FT:";
        for(int i = 0; i < frame_size; i++){
            frame_t* frame = &frame_table[i];
            if(frame->vpage != -1){
                printf(" %d:%d", frame->pid, frame->vpage);
            }else{
                cout << " *";
            }
        }
        cout << endl;
    }
    if(statistic_option){
        unsigned long long cost = 0;
        for(int i = 0; i < proc_vector.size(); i++){
            Process* proc = proc_vector[i];
            summary_t* pstats = &proc->summary;
            printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
                   proc->pid,
                   pstats->unmaps, pstats->maps, pstats->ins, pstats->outs,
                   pstats->fins, pstats->fouts, pstats->zeros,
                   pstats->segv, pstats->segprot);
            cost += pstats->maps * 300 + pstats->unmaps * 400 + pstats->ins * 3100 + pstats->outs * 2700 +
                       pstats->fins * 2800 + pstats->fouts * 2400 + pstats->zeros * 140 + pstats->segv * 340 +
                       pstats->segprot * 420;
        }
        cost += read_write * 1 + ctx_switches * 130 + process_exits * 1250;
        printf("TOTALCOST %llu %llu %llu %llu %lu\n",
               inst_count, ctx_switches, process_exits, cost, sizeof(pte_t));
    }
}
void simulation(){

    initialize_frame_table(frame_size);
    Pager* THE_PAGER = set_pager(pager);
    initialize_free_pool();

/*******************************************************************************/

    for(int i = 0; i < instruction_list.size(); i++) {

        string operation = instruction_list.at(i).first;
        int second = instruction_list.at(i).second;
        instruction_count += 1;

        //context switch
        if (operation == "c") {
            ctx_switches += 1;
            curr_proc = proc_vector.at(second);
            if(o_option) printf("%d: ==> c %d\n", i, curr_proc->pid);
        }
        //read and write
        if (operation == "r" || operation == "w") {

            read_write += 1;
            int vpage = second;
            if(o_option) printf("%d: ==> %s %d\n", i, operation.c_str(), vpage);
            pte_t *pte = &(curr_proc->page_table[vpage]);

            if (!(pte->CONFIGURATED)) {
                configurate_pte(curr_proc->pid, vpage);
            }

            if (!(pte->VALID)) {

                if (not_sevg(curr_proc, vpage) == false) {
                    curr_proc->summary.segv +=1;
                    if(o_option) cout << " SEGV" << endl;
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
                else {

                    // 1) choose victim frame
                    victim_frame = THE_PAGER->select_victim_frame();
                    int prev_proc_id = victim_frame->pid;
                    Process* prev_proc = proc_vector[prev_proc_id];
                    pte_t *prev_pte = &proc_vector.at(prev_proc_id)->page_table[victim_frame->vpage];

                    // 2) unmap pte
                    if(o_option) cout << " UNMAP " << victim_frame->pid << ":" << victim_frame->vpage << endl;
                    prev_proc->summary.unmaps +=1;

                    // 3) check modified and file mapped
                    if (prev_pte->MODIFIED && prev_pte->FILEMAPPED) {
                        prev_proc->summary.fouts += 1;
                        if(o_option) cout << " FOUT" << endl;
                    } else if (prev_pte->MODIFIED) {
                        prev_pte->PAGEDOUT = 1;
                        prev_proc->summary.outs += 1;
                        if(o_option) cout << " OUT" << endl;
                    }

                    // 4) reset previous pte bit
                    prev_pte->VALID = 0;
                    prev_pte->MODIFIED = 0;
                    prev_pte->REFERENCED = 0;
                    prev_pte->frame_number = 0;
                }

                // 5) new_pte <-> frame page
                victim_frame->vpage = vpage;
                victim_frame->pid = curr_proc->pid;
                pte->frame_number = victim_frame->frame_id;
                pte->VALID = 1;

                // 6) check new_pte bit
                if (pte->FILEMAPPED) {
                    curr_proc->summary.fins += 1;
                    if(o_option) cout << " FIN" << endl;
                } else if (pte->PAGEDOUT) {
                    curr_proc->summary.ins += 1;
                    if(o_option) cout << " IN" << endl;
                } else {
                    curr_proc->summary.zeros += 1;
                    if(o_option) cout << " ZERO" << endl;
                }

                // 7) map frame page
                curr_proc->summary.maps += 1;
                THE_PAGER->reset_counter(victim_frame);
                if(o_option) cout << " MAP " + to_string(victim_frame->frame_id) << endl;
            }

            // 8) r/w operation => pte has been referenced
            pte->REFERENCED = 1;

            // 9) w operation check write_protect => MODIFIED or SEGPROT
            if(pte->WRITE_PROTECT && operation == "w"){
                curr_proc->summary.segprot += 1;
                if(o_option) cout << " SEGPROT" << endl;
            }else if(operation == "w"){
                pte->MODIFIED = 1;
                //cout << "proc_id: " + to_string(curr_proc->pid) + " page M : " + to_string(curr_proc->page_table[vpage].MODIFIED) << endl;
            }

        }
        //exit
        if (operation == "e"){

            if(o_option) printf("%d: ==> e %d\n", i, curr_proc->pid);
            if(o_option) printf("EXIT current process %d\n", curr_proc->pid);
            process_exits += 1;

            // check page table and reset bits
            for(int i = 0; i < curr_proc->page_table.size(); i++){
                summary_t summary = curr_proc->summary;
                pte_t* pte = &curr_proc->page_table[i];

                // 1) UNMAP pte from frame page
                if(pte->VALID){
                    if(o_option) printf(" UNMAP %d:%d\n", curr_proc->pid, i );
                    curr_proc->summary.unmaps += 1;
                    frame_t* frame = &frame_table[pte->frame_number];
                    frame->pid = -1;
                    frame->vpage = -1;
                    frame->counter = 0;
                    frame->last_used_time = 0;
                    frame->dirty = false;
                    free_pool.push_back(frame);

                    // 2) check MODIFIED and FILEMAPPED
                    if (pte->MODIFIED && pte->FILEMAPPED){
                        curr_proc->summary.fouts++;
                        if (o_option) cout << " FOUT" << endl;
                    }
                }

                // 3) reset pte bits
                pte->VALID = 0;
                pte->frame_number = -1;
                pte->WRITE_PROTECT = 0;
                pte->CONFIGURATED = 0;
                pte->PAGEDOUT = 0;
                pte->REFERENCED = 0;
                pte->FILEMAPPED = 0;
                pte->MODIFIED = 0;
            }

            // 3) curr_proc = nullptr
            curr_proc = nullptr;
        }
    }
}

int main(int argc, char *argv[]){

    int c;
    while ((c = getopt(argc, argv, "f:a:o:")) != -1){
        switch (c) {
            case 'f':
                frame_size = stoi(optarg);
                break;
            case 'a':
                pager = optarg;
                break;
            case 'o':
                options = optarg;
                for (auto &ch : options) {
                    switch (ch) {
                        case 'O':
                            o_option = true;
                            break;
                        case 'P':
                            page_table_option = true;
                            break;
                        case 'F':
                            frame_table_option = true;
                            break;
                        case 'S':
                            statistic_option = true;
                            break;
                        default:
                            break;
                    }
                }
            case '?':
                printf("error optopt: %c\n", optopt);
                printf("error opterr: %d\n", opterr);
                break;
            default:
                exit(EXIT_FAILURE);
                break;
        }
    }
//    cout << "frame_size: " + to_string(frame_size) << endl;
//    cout << "pager: " + pager << endl;
//    cout << "options: " + options << endl;
    readFile(argv[argc - 2], argv[argc - 1]);
    simulation();
    print_output();
}