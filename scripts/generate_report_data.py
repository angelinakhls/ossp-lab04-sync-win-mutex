import csv
import matplotlib.pyplot as plt

with open('../data/experiment_results.csv', newline='') as f:
    reader = csv.DictReader(f)
    rows = list(reader)

x = [float(r['timeout_ms']) for r in rows]
y = [float(r['avg_time_ms']) for r in rows]

plt.figure(figsize=(10, 5))
plt.plot(x, y, marker='o')
plt.title('Average execution time vs timeout')
plt.xlabel('Timeout (ms)')
plt.ylabel('Average time (ms)')
plt.grid(True)
plt.show()
