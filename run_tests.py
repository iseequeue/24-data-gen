import subprocess
import os
import json
from typing import Dict
from tqdm import tqdm
import numpy as np
import matplotlib.pyplot as plt


def validate_path(path):
    if not os.path.exists(path):
        raise FileNotFoundError(f"File not found: {path}")
    
def validate_data(data : Dict):
    validate_path(data['robots'])
    validate_path(data['objects'])
    validate_path(data['scene'])
    validate_path(data['obstacles'])

    print("SUCCESS")


directory = "/home/alex/multitask/24-data-gen/in/random"
folders = [item for item in os.listdir(directory) if os.path.isdir(os.path.join(directory, item))]
print(f"Папки {len(folders)}:", folders)


def clear_dir(directory):
    for filename in os.listdir(directory):
            file_path = os.path.join(directory, filename)
            if os.path.isfile(file_path):
                os.remove(file_path)
                print(f"Deleted: {file_path}")

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
            except Exception as e:
                print(f"Ошибка при чтении файла {filename}: {e}")

    # Сохраняем объединённые данные
    try:
        with open(output_file, "w", encoding="utf-8") as f:
            json.dump(merged_data, f, indent=4, ensure_ascii=False)
        print(f"Объединённый файл сохранён: {output_file}")
    except Exception as e:
        print(f"Ошибка при сохранении файла: {e}")

# Пример использования
input_directory = "/home/alex/multitask/24-data-gen/log"
output_filename = "/home/alex/multitask/24-data-gen/result.json"
merge_robot_results(input_directory, output_filename)

def sort_data(input_file, output_file):
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


scene = 'random'

directory = "/home/alex/multitask/24-data-gen/log"

seeds = [879]
for folder in tqdm(folders):
    num_objects = get_num_obj(f'/home/alex/multitask/24-data-gen/in/{scene}/{folder}/obj_path.json')    
    for seed in seeds:
        

        args = { 'robots':    f'in/{scene}/{folder}/robot_path.json',
                  'objects':   f'in/{scene}/{folder}/obj_path.json',
                  'scene':     f'in/{scene}/{folder}/scene.g',
                  'obstacles': f'in/{scene}/{folder}/obstacles_file.json'}

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
            #'-scene_path',      args['scene'],
            #'-obstacle_path',   args['obstacles']
        ]
        # validate_data(args)

        clear_dir(directory)
        subprocess.run(command)
        merge_robot_results("/home/alex/multitask/24-data-gen/log", "/home/alex/multitask/24-data-gen/result.json")
        sort_data("/home/alex/multitask/24-data-gen/result.json", f"/home/alex/multitask/24-data-gen/stats_random/sipp_{folder}.json")
        

        command = [
            './rrt.exe',
            '-pnp',             'true',
            '-mode',            'random_search',
            '-seed',            str(seed),
            '-robot_path',      args['robots'],
            '-obj_path',        args['objects'],
            '-attempt_komo',    'false',
            '-display',         'false',
            '-export_images',   'false',
            # '-scene_path',      args['scene'],
            # '-obstacle_path',   args['obstacles']
        ]
        clear_dir(directory)
        subprocess.run(command)   
        merge_robot_results("/home/alex/multitask/24-data-gen/log", "/home/alex/multitask/24-data-gen/result.json")
        sort_data("/home/alex/multitask/24-data-gen/result.json", f"/home/alex/multitask/24-data-gen/stats_random/rrt_{folder}.json")    