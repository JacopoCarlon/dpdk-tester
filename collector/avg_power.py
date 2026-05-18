import pandas as pd
import argparse

def main():
    parser = argparse.ArgumentParser(description='Calculate power consumption from RAPL data')
    parser.add_argument('-i', '--input', required=True, help='Input data file')
    parser.add_argument('-o', '--output', required=True, help='Output results file')
    args = parser.parse_args()

    # RAPL energy counters (update if needed)
    PKG_MAX_ENERGY = 262143328850
    DRAM_MAX_ENERGY = 65712999613

    try:
        # Read raw data
        df = pd.read_csv(args.input, sep=' ', header=None, 
                        names=['timestamp', 'pkg_uj', 'dram_uj'])
        
        # Calculate energy deltas with overflow protection
        df['pkg_delta_uj'] = df['pkg_uj'].diff().apply(
            lambda x: x if x > 0 else x + PKG_MAX_ENERGY + 1)
        df['dram_delta_uj'] = df['dram_uj'].diff().apply(
            lambda x: x if x > 0 else x + DRAM_MAX_ENERGY + 1)
        
        # Calculate time delta and power
        df['time_delta'] = df['timestamp'].diff()
        df['pkg_power_w'] = df['pkg_delta_uj'] / df['time_delta'] / 1e6
        df['dram_power_w'] = df['dram_delta_uj'] / df['time_delta'] / 1e6

        # Create interval timestamps
        df['start_time'] = df['timestamp'].shift(1)
        df['end_time'] = df['timestamp']
        
        # Filter valid intervals (skip first row with NaNs)
        intervals = df[1:][['start_time', 'end_time', 
                          'pkg_power_w', 'dram_power_w']]

        w_time = 24
        w_power = 12
        w_socket = 16
        w_dram = 16
        # Write per-interval data
        with open(args.output, 'w') as f:
            # Write individual intervals
	        # f.write("start_time end_time pkg_power_w dram_power_w\n")
            # for _, row in intervals.iterrows():
                # f.write(f"{row['start_time']:.9f} {row['end_time']:.9f} "
                        #f"{row['pkg_power_w']:.8f} {row['dram_power_w']:.8f}\n")
            

            # Header
            f.write(
                f"{'start_time':<{w_time}} "
                f"{'end_time':<{w_time}} "
                f"{'pkg_power_w':<{w_power}} "
                f"{'dram_power_w':<{w_power}}\n"
            )

            # Rows
            for _, row in intervals.iterrows():
                f.write(
                    f"{row['start_time']:<{w_time}.9f} "
                    f"{row['end_time']:<{w_time}.9f} "
                    f"{row['pkg_power_w']:<{w_power}.8f} "
                    f"{row['dram_power_w']:<{w_power}.8f}\n"
                )

            # Calculate global statistics
            pkg = intervals['pkg_power_w']
            dram = intervals['dram_power_w']
            
            stats = {
                'pkg_min': pkg.min(),
                'pkg_max': pkg.max(),
                'pkg_avg': pkg.mean(),
                'pkg_std': pkg.std(),
                'dram_min': dram.min(),
                'dram_max': dram.max(),
                'dram_avg': dram.mean(),
                'dram_std': dram.std()
            }
            
            # Write global stats
            f.write("\n=========GLOBAL STATS=========\n")
            f.write(
                f"{'pkg_min':<{w_socket}} "
                f"{'pkg_max':<{w_socket}} "
                f"{'pkg_avg':<{w_socket}} "
                f"{'pkg_std':<{w_socket}}\n"
            )

            f.write(f"{stats['pkg_min']:<{w_socket}.8f} "
                    f"{stats['pkg_max']:<{w_socket}.8f} "
                    f"{stats['pkg_avg']:<{w_socket}.8f} "
                    f"{stats['pkg_std']:<{w_socket}.8f} \n")
            
            f.write(
                f"{'dram_min':<{w_dram}} "
                f"{'dram_max':<{w_dram}} "
                f"{'dram_avg':<{w_dram}} "
                f"{'dram_std':<{w_dram}}\n"
            )
            f.write(f"{stats['dram_min']:<{w_dram}.8f} "
                    f"{stats['dram_max']:<{w_dram}.8f} "
                    f"{stats['dram_avg']:<{w_dram}.8f} "
                    f"{stats['dram_std']:<{w_dram}.8f}\n")

    except Exception as e:
        print(f"Error processing data: {e}")

if __name__ == "__main__":
    main()
    


