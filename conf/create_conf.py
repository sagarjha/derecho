import shutil
import sys
import os

def replace_id_port_domain(cfg_file, i):
    id_and_port = \
        ['local_id', 'gms_port', 'rpc_port', 'sst_port', 'rdmc_port']

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
            elif words[0] == 'domain':
                words[-1] = 'lo'
                lines[idx] = " ".join(words) + '\n'

    with open(cfg_file, 'w') as f:
        f.writelines(lines)

if __name__ == '__main__':
    assert len(sys.argv) == 3
    dst_abs_path = sys.argv[1]
    max_proc_num = int(sys.argv[2]) # maximum process number
    cfg_abs_path = os.path.abspath('derecho.cfg')

    print(dst_abs_path, max_proc_num, cfg_abs_path)

    for i in range(max_proc_num):
        dir_name = 'process'+str(i)
        proc_dir = os.path.join(dst_abs_path, dir_name)
        if not os.path.exists(proc_dir):
            os.mkdir(proc_dir)
            print('Directory ' + dir_name + ' Created')
            shutil.copy2(cfg_abs_path, proc_dir)
            print('derecho.cfg Created')
            cfg_file = os.path.join(proc_dir, 'derecho.cfg')
            replace_id_port_domain(cfg_file, i)
        else:
            print('Directory ', dir_name, ' already exists')


