import shutil
import sys
import os

def update_id_port_domain(cfg_file, i):
    # configurations to update
    id_and_port = \
        ['local_id', 'gms_port', 'rpc_port', 'sst_port', 'rdmc_port']
    domain = ['domain']

    with open(cfg_file, 'r') as f:
        lines = f.readlines()

    for idx, line in enumerate(lines):
        words = line.strip().split()
        if '#' in words or len(words) < 3:
            continue
        else:
            if words[0] in id_and_port:
                words[-1] = str(int(words[-1]) + i)
                lines[idx] = " ".join(words) + '\n'
            elif words[0] in domain:
                words[-1] = 'lo'
                lines[idx] = " ".join(words) + '\n'

    with open(cfg_file, 'w') as f:
        f.writelines(lines)

def update_semaphore_and_shared_memory_names(cfg_file, i):
    semkey_and_shmkey = ['model_sem_name', 'model_shm_name', 'grad_sem_name']

    with open(cfg_file, 'r') as f:
        lines = f.readlines()

    for idx, line in enumerate(lines):
        words = line.strip().split()
        if '#' in words or len(words) < 3:
            continue
        else:
            if words[0] in semkey_and_shmkey:
                words[-1] = (words[-1]) + "_" + str(i)
                lines[idx] = " ".join(words) + '\n'

    with open(cfg_file, 'w') as f:
        f.writelines(lines)

if __name__ == '__main__':
    assert len(sys.argv) == 3
    dst_abs_path = sys.argv[1]
    max_proc_num = int(sys.argv[2]) # maximum process number
    assert os.path.exists(os.path.abspath('derecho.cfg'))
    cfg_abs_path = os.path.abspath('derecho.cfg')

    # 1. Create directories named process0, process1, ...
    # 2. For each iteration, update id, port, and domain
    #    in each derecho.conf in the created directories.
    # 3. Append semshk and smshk at the updated file.
    for i in range(max_proc_num):
        dir_name = 'process'+str(i)
        proc_dir = os.path.join(dst_abs_path, dir_name)
        if not os.path.exists(proc_dir):
            os.mkdir(proc_dir)
            print('Directory ' + dir_name + ' Created')
            shutil.copy2(cfg_abs_path, proc_dir)
            print('derecho.cfg Created')
            cfg_file = os.path.join(proc_dir, 'derecho.cfg')
            update_id_port_domain(cfg_file, i)
            update_semaphore_and_shared_memory_names(cfg_file, i)
        else:
            print('Directory ', dir_name, ' already exists')

