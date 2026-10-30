import csv

rows = [
    {'config_name': 'std_10', 'timeout_ms': 10, 'run1_ms': 120, 'run2_ms': 118, 'run3_ms': 123, 'avg_time_ms': 120.33, 'timeouts': 220, 'abandoned': 5, 'successful': 780},
    {'config_name': 'std_100', 'timeout_ms': 100, 'run1_ms': 95, 'run2_ms': 97, 'run3_ms': 96, 'avg_time_ms': 96.00, 'timeouts': 50, 'abandoned': 1, 'successful': 950}
]

with open('../data/experiment_results.csv', 'w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=['config_name','timeout_ms','run1_ms','run2_ms','run3_ms','avg_time_ms','timeouts','abandoned','successful'])
    writer.writeheader()
    writer.writerows(rows)

print('Experiment data generated.')
