import subprocess
import os
import json
from typing import Dict
from tqdm import tqdm
import numpy as np
import matplotlib.pyplot as plt
from concurrent.futures import ProcessPoolExecutor
NUM_CPUS = 126


def validate_path(path):
    if not os.path.exist(path):
        raise FileNotFoundError(f"File not found: {path}")
    
def validate_data(data : Dict):
    validate_path(data['robots'])
    validate_path(data['objects'])
    validate_path(data['scene'])
    validate_path(data['obstacles'])

    print("SUCCESS")





def get_num_obj(directory):
     with open(directory, 'r') as f:
        data = json.load(f)
        return len(data['objects'])
     

def merge_robot_results(input_dir, output_file):
    """
    Объединяет все JSON-файлы из input_dir в один файл output_file.
    Каждый файл должен содержать данные одного робота.
    """
    merged_data = []

    # Перебираем все файлы в директории
    for filename in os.listdir(input_dir):
        if filename.endswith(".json"):
            file_path = os.path.join(input_dir, filename)
            try:
                with open(file_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    if data['success'] == 1:
                        merged_data.append(data)
                    # else:
                    #     merged_data = []
                    #     break
            except Exception as e:
                print(f"Ошибка при чтении файла {filename}: {e}")

    # Сохраняем объединённые данные
    try:
        if len(merged_data) !=0:
            with open(output_file, "w", encoding="utf-8") as f:
                json.dump(merged_data, f, indent=4, ensure_ascii=False)
        # print(f"Объединённый файл сохранён: {output_file}")
    except Exception as e:
        print(f"Ошибка при сохранении файла: {e}")


def sort_data(input_file, output_file):
    if not os.path.exists(input_file):
        return
    with open(input_file, "r", encoding="utf-8") as f:
        data = json.load(f)

    robot_dict = {}

    for entry in data:
        robot_name = entry["name"]
        if robot_name not in robot_dict:
            robot_dict[robot_name] = []
        robot_dict[robot_name].append(entry)

    for robot in robot_dict:
        robot_dict[robot].sort(key=lambda x: x["start_time"])

    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(robot_dict, f, indent=4, ensure_ascii=False)

def process_sirrt(scene,folder,seed,args):
    log_dir = f'log/{scene}/sirrt/{folder}/{seed}/'
    os.makedirs(log_dir, exist_ok = False)

    command = [
        './sipp.exe',
        '-pnp',             'true',
        '-mode',            'random_search',
        '-seed',            str(seed),
        '-robot_path',      args['robots'],
        '-obj_path',        args['objects'],
        '-attempt_komo',    'false',
        '-display',         'false',
        '-export_images',   'false',
        '-log_dir',         "'"+log_dir+"'",
        '-verbosity',       '0',
        #'-scene_path',      args['scene'],
        #'-obstacle_path',   args['obstacles']
    ]
    # validate_data(args)
    if 'scene' in args:
        command.append('-scene_path')
        command.append(args['scene'])
    if 'obstacles' in args:
        command.append('-obstacle_path')
        command.append(args['obstacles'])
    subprocess.run(' '.join(command),shell=True,stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)   

    result_file_path = os.path.join(log_dir,"result.json")
    merge_robot_results(log_dir, result_file_path)
    sort_data(result_file_path, f"./result/stats_{scene}/sipp_{folder}_{seed}.json")
    
    
def process_rrt(scene,folder,seed,args,c):
    log_dir = f'log/{scene}/rrt/{folder}/{seed}/'
    os.makedirs(log_dir, exist_ok = False)

    command = [
        './rrt.exe',
        '-pnp',             'true',
        '-mode',            "random_search",
        '-seed',            str(seed),
        '-robot_path',      args['robots'],
        '-obj_path',        args['objects'],
        '-attempt_komo',    'false',
        '-display',         'false',
        '-export_images',   'false',
        '-log_dir',         "'"+log_dir+"'",
        '-verbosity',       '0',
        # '-scene_path',      args['scene'],
        # '-obstacle_path',   args['obstacles']
    ]
    if 'scene' in args:
        command.append('-scene_path')
        command.append(args['scene'])
    if 'obstacles' in args:
        command.append('-obstacle_path')
        command.append(args['obstacles'])
    subprocess.run(' '.join(command),shell=True,stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)   
    result_file_path = os.path.join(log_dir,"result.json")
    merge_robot_results(log_dir, result_file_path)
    sort_data(result_file_path, f"./result/stats_{scene}/rrt_{folder}_{seed}.json")   
    
    print("Done test №",c)
    
    
def process(scene,folder,seed,args,c):
    log_dir_rrt = f'log/{scene}/rrt/{folder}/{seed}/'
    log_dir_sirrt = f'log/{scene}/sirrt/{folder}/{seed}/'
    os.makedirs(log_dir_rrt, exist_ok = False)
    os.makedirs(log_dir_sirrt, exist_ok = False)

    command = [
        './x.exe',
        '-pnp',             'true',
        '-mode',            "random_search",
        '-seed',            str(seed),
        '-robot_path',      args['robots'],
        '-obj_path',        args['objects'],
        '-attempt_komo',    'false',
        '-display',         'false',
        '-export_images',   'false',
        '-log_dir_strrt',         "'"+log_dir_rrt+"'",
        '-log_dir_sirrt',         "'"+log_dir_sirrt+"'",
        '-verbosity',       '0',
        # '-scene_path',      args['scene'],
        # '-obstacle_path',   args['obstacles']
    ]
    if 'scene' in args:
        command.append('-scene_path')
        command.append(args['scene'])
    if 'obstacles' in args:
        command.append('-obstacle_path')
        command.append(args['obstacles'])
    subprocess.run(' '.join(command),shell=True,stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)   
    result_file_path = os.path.join(log_dir_rrt,"result.json")
    merge_robot_results(log_dir_rrt, result_file_path)
    sirrt_result_file_path = os.path.join(log_dir_sirrt,"result.json")
    merge_robot_results(log_dir_sirrt, sirrt_result_file_path)
    sort_data(result_file_path, f"./result/stats_{scene}/rrt_{folder}_{seed}.json")   
    sort_data(sirrt_result_file_path, f"./result/stats_{scene}/sipp_{folder}_{seed}.json")   
    
    print("Done test №",c)
    

def main():
    
    
            
    futures = []

        
    with ProcessPoolExecutor(max_workers=NUM_CPUS) as executor:
        c= 0
        os.makedirs('./log',exist_ok=False)
        
        os.makedirs('./result',exist_ok=False)
        os.makedirs('./result/stats_random',exist_ok=False)
        os.makedirs('./result/stats_conveyor',exist_ok=False)
        os.makedirs('./result/stats_husky',exist_ok=False)
        os.makedirs('./result/stats_shelf',exist_ok=False)
        directory = "./in/random"
        folders = [item for item in os.listdir(directory) if os.path.isdir(os.path.join(directory, item))]
        print(f"Папки {len(folders)}:", folders)

        scene = 'random'

        seeds = [1,42,1902,503467340,13241,132412,3255234,1324,2364]
        os.makedirs(f'./log/{scene}',exist_ok=False)
        
        for folder in tqdm(folders):
            num_objects = get_num_obj(f'./in/{scene}/{folder}/obj_path.json')    
            for seed in seeds:
                c+=1
                args = { 'robots':    "'"+f'in/{scene}/{folder}/robot_path.json'+"'",
                'objects':   "'"+f'in/{scene}/{folder}/obj_path.json'+"'",
                # 'scene':     f'in/{scene}/{folder}/scene.g',
                # 'obstacles': f'in/{scene}/{folder}/obstacles_file.json'
                }
                futures.append(executor.submit(process, scene,folder,seed,args,c))
            
                
                
        directory = "./in/conveyor"
        folders = [item for item in os.listdir(directory) if os.path.isdir(os.path.join(directory, item))]
        print(f"Папки {len(folders)}:", folders)

        scene = 'conveyor'

        seeds = [1]
        os.makedirs(f'./log/{scene}',exist_ok=False)
        for folder in tqdm(folders):
            num_objects = get_num_obj(f'./in/{scene}/{folder}/obj_path.json')    
            for seed in seeds:
                c+=1
                args = { 'robots':    "'"+f'in/{scene}/{folder}/robot_path.json'+"'",
                'objects':   "'"+f'in/{scene}/{folder}/obj_path.json'+"'",
                'scene':     "'"+f'in/{scene}/{folder}/scene.g'+"'",
                # 'obstacles': f'in/{scene}/{folder}/obstacles_file.json'
                }
                futures.append(executor.submit(process, scene,folder,seed,args,c))
            
                
        directory = "./in/husky"
        folders = [item for item in os.listdir(directory) if os.path.isdir(os.path.join(directory, item))]
        print(f"Папки {len(folders)}:", folders)

        scene = 'husky'

        seeds = [1]
        os.makedirs(f'./log/{scene}',exist_ok=False)
        for folder in tqdm(folders):
            num_objects = get_num_obj(f'./in/{scene}/{folder}/obj_path.json')    
            for seed in seeds:
                c+=1
                args = { 'robots':    "'"+f'in/{scene}/{folder}/robot_path.json'+"'",
                'objects':   "'"+f'in/{scene}/{folder}/obj_path.json'+"'",
                'scene':     "'"+f'in/{scene}/0/scene.g'+"'",
                # 'obstacles': f'in/{scene}/{folder}/obstacles_file.json'
                }
                futures.append(executor.submit(process, scene,folder,seed,args,c))
            
                
        directory = "./in/shelf"
        folders = [item for item in os.listdir(directory) if os.path.isdir(os.path.join(directory, item))]
        print(f"Папки {len(folders)}:", folders)

        scene = 'shelf'

        seeds = [1]
        os.makedirs(f'./log/{scene}',exist_ok=False)
        for folder in tqdm(folders):
            num_objects = get_num_obj(f'./in/{scene}/{folder}/obj_path.json')    
            for seed in seeds:
                c+=1
                args = { 'robots':    "'"+f'in/{scene}/{folder}/robot_path.json'+"'",
                'objects':   "'"+f'in/{scene}/{folder}/obj_path.json'+"'",
                'scene':     "'"+f'in/{scene}/{folder}/scene.g'+"'",
                'obstacles': "'"+f'in/{scene}/{folder}/obstacles_file.json'+"'"
                }
                futures.append(executor.submit(process, scene,folder,seed,args,c))
            
        
    for future in tqdm(futures):
        future.result()
            
if __name__=="__main__":
    main() 