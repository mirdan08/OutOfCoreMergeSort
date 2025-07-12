import matplotlib.pyplot as plt
import pandas as pd
import argparse
import os

def analyze_csv(input_file, save_plots=False, output_dir="plots"):
    # Read CSV
    df = pd.read_csv(input_file)

    # Group by (max_payload_size, records_number), aggregate over iterations
    stats = df.groupby(["max_payload_size", "records_number"])["time(ms)"] \
              .agg(["mean", "var"]).reset_index()

    print("\nMean and Variance of time(ms) for each (max_payload_size, records_number):")
    print(stats)

    # Create output directory if needed
    if save_plots:
        os.makedirs(output_dir, exist_ok=True)

    # Plot mean as a heatmap-like scatter plot
    plt.figure(figsize=(10, 6))
    scatter = plt.scatter(
        stats["max_payload_size"],
        stats["records_number"],
        c=stats["mean"],
        cmap="viridis",
        s=100,
        edgecolor="k"
    )
    plt.colorbar(scatter, label="Mean Time (ms)")
    plt.title("Mean Time (ms) per (max_payload_size, records_number)")
    plt.xlabel("Max Payload Size")
    plt.ylabel("Records Number")
    plt.grid(True)

    if save_plots:
        mean_plot_path = os.path.join(output_dir, "mean_time_scatter.png")
        plt.savefig(mean_plot_path)
        print(f"Saved mean scatter plot to {mean_plot_path}")
    else:
        plt.show()

    # Plot variance as a heatmap-like scatter plot
    plt.figure(figsize=(10, 6))
    scatter = plt.scatter(
        stats["max_payload_size"],
        stats["records_number"],
        c=stats["var"],
        cmap="plasma",
        s=100,
        edgecolor="k"
    )
    plt.colorbar(scatter, label="Variance (ms²)")
    plt.title("Variance of Time (ms²) per (max_payload_size, records_number)")
    plt.xlabel("Max Payload Size")
    plt.ylabel("Records Number")
    plt.grid(True)

    if save_plots:
        var_plot_path = os.path.join(output_dir, "variance_time_scatter.png")
        plt.savefig(var_plot_path)
        print(f"Saved variance scatter plot to {var_plot_path}")
    else:
        plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Analyze CSV and plot mean/variance of time(ms) per iteration.")
    parser.add_argument("--input", "-i", required=True, help="Path to the input CSV file.")
    parser.add_argument("--save-plots", "-s", action="store_true", help="Save plots instead of showing them.")
    parser.add_argument("--output-dir", "-o", default="plots", help="Directory to save plots (default: ./plots)")

    args = parser.parse_args()

    analyze_csv(args.input, save_plots=args.save_plots, output_dir=args.output_dir)
