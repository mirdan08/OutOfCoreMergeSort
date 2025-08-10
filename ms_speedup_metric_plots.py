import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import argparse
import os
import sys

def load_and_average(parallel_csv, sequential_csv):
    try:
        # Load CSVs
        df_par = pd.read_csv(parallel_csv).dropna(subset=['time(ms)'])
        df_seq = pd.read_csv(sequential_csv).dropna(subset=['time(ms)'])
    except Exception as e:
        print(f"Error reading CSV files: {e}")
        sys.exit(1)
    # Average parallel times over iterations
    par_grouped = df_par.groupby(['max_payload_size', 'records_number', 'n_threads'], as_index=False)['time(ms)'].mean()

    # Average sequential times over iterations
    seq_grouped = df_seq.groupby(['max_payload_size', 'records_number'], as_index=False)['time(ms)'].mean()
    seq_grouped = seq_grouped.rename(columns={'time(ms)': 'seq_time(ms)'})
    #print(par_grouped)
    #print(seq_grouped)
    # Merge parallel and sequential data on (max_payload_size, records_number)
    merged = pd.merge(par_grouped, seq_grouped ,on=['max_payload_size', 'records_number'],how='outer')

    par_grouped.groupby(['max_payload_size', 'records_number'])
    # Compute Speedup and Efficiency
    merged['speedup'] = merged['seq_time(ms)'] / merged['time(ms)']
    merged['efficiency'] = merged['speedup'] / merged['n_threads']
    #print(merged)
    return merged

def plot_curves(merged, output_dir, show):
    sns.set(style="whitegrid")
    unique_configs = merged[['max_payload_size', 'records_number']].drop_duplicates()

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    for _, row in unique_configs.iterrows():
        print(row)
        payload = row['max_payload_size']
        records = row['records_number']

        subset = merged[(merged['max_payload_size'] == payload) &
                        (merged['records_number'] == records)]
        print(subset)
        plt.figure(figsize=(10, 5))

        # Plot Speedup
        plt.subplot(1, 2, 1)
        plt.title(f"Speedup - Records: {records}, Payload: {payload}")
        plt.plot(subset['n_threads'], subset['speedup'], marker='o', color='b')
        plt.xlabel("Number of Threads")
        plt.ylabel("Speedup")
        plt.xticks(subset['n_threads'])
        plt.grid(True)

        # Plot Efficiency
        plt.subplot(1, 2, 2)
        plt.title(f"Efficiency - Records: {records}, Payload: {payload}")
        plt.plot(subset['n_threads'], subset['efficiency'], marker='o', color='g')
        plt.xlabel("Number of Threads")
        plt.ylabel("Efficiency")
        plt.xticks(subset['n_threads'])
        plt.grid(True)

        plt.tight_layout()
        filename = f"speedup_efficiency_records_{records}_payload_{payload}.png"
        filepath = os.path.join(output_dir, filename)
        plt.savefig(filepath)
        print(f"Saved plot to {filepath}")

        if show:
            plt.show()
        else:
            plt.close()

def main():
    parser = argparse.ArgumentParser(description="Compute and plot speedup/efficiency from parallel and sequential runs.")
    parser.add_argument("parallel_csv", help="CSV file from parallel runs.")
    parser.add_argument("sequential_csv", help="CSV file from sequential runs.")
    parser.add_argument("--output-dir", default="plots", help="Directory to save plots (default: 'plots').")
    parser.add_argument("--show", action="store_true", help="Show plots interactively after saving.")
    args = parser.parse_args()

    merged = load_and_average(args.parallel_csv, args.sequential_csv)
    plot_curves(merged, args.output_dir, args.show)

if __name__ == "__main__":
    main()
