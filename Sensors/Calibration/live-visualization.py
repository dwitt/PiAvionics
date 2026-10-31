"""
Example of how to use the magnetometer calibration with real-time visualization
"""

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from mpl_toolkits.mplot3d import Axes3D
from IPython.display import display, clear_output
import ipywidgets as widgets
import time
import threading

# Import our MagnetometerCalibration class
from magnetometer_calibration import MagnetometerCalibration

class MagnetometerVisualizer:
    """
    A class for visualizing magnetometer data in real-time with
    calibration capabilities.
    """
    
    def __init__(self):
        """Initialize the visualizer."""
        self.cal = MagnetometerCalibration()
        self.fig = None
        self.ax = None
        self.scatter = None
        self.animation = None
        self.is_running = False
        
        # Create a figure for real-time plotting
        self.create_figure()
        
    def create_figure(self):
        """Create the figure for visualization."""
        self.fig = plt.figure(figsize=(10, 8))
        self.ax = self.fig.add_subplot(111, projection='3d')
        
        # Initialize empty scatter plot
        self.scatter = self.ax.scatter([], [], [], c='b', marker='o', alpha=0.5, s=20)
        
        # Add a sphere to represent the ideal calibrated data
        u = np.linspace(0, 2 * np.pi, 100)
        v = np.linspace(0, np.pi, 100)
        
        sphere_x = np.outer(np.cos(u), np.sin(v))
        sphere_y = np.outer(np.sin(u), np.sin(v))
        sphere_z = np.outer(np.ones(np.size(u)), np.cos(v))
        
        # Plot sphere with low alpha for reference
        self.ax.plot_surface(sphere_x, sphere_y, sphere_z, 
                            color='gray', alpha=0.1, linewidth=0)
        
        # Set labels
        self.ax.set_xlabel('X')
        self.ax.set_ylabel('Y')
        self.ax.set_zlabel('Z')
        
        # Set limits initially
        self.ax.set_xlim(-2, 2)
        self.ax.set_ylim(-2, 2)
        self.ax.set_zlim(-2, 2)
        
        self.ax.set_title('Real-time Magnetometer Calibration')
        
    def update_plot(self, frame):
        """Update the plot with new data."""
        if not self.cal.is_collecting or len(self.cal.data_buffer) < 3:
            return self.scatter,
            
        # Get data from buffer
        data = np.array(list(self.cal.data_buffer))
        
        # Apply calibration if available
        if self.cal.is_calibrated:
            calibrated_data = self.cal.apply_calibration(data)
            
            # Use calibrated data for plotting
            self.scatter._offsets3d = (
                calibrated_data[:, 0], 
                calibrated_data[:, 1], 
                calibrated_data[:, 2]
            )
            
            # Update color to show it's calibrated
            self.scatter.set_color('green')
        else:
            # Use raw data
            self.scatter._offsets3d = (data[:, 0], data[:, 1], data[:, 2])
            self.scatter.set_color('blue')
            
        # Auto-adjust limits if needed
        if frame % 30 == 0:  # Only adjust every 30 frames for performance
            x_range = (np.min(data[:, 0]), np.max(data[:, 0]))
            y_range = (np.min(data[:, 1]), np.max(data[:, 1]))
            z_range = (np.min(data[:, 2]), np.max(data[:, 2]))
            
            max_range = max(
                x_range[1] - x_range[0],
                y_range[1] - y_range[0],
                z_range[1] - z_range[0]
            )
            
            # Add some padding
            padding = max_range * 0.1
            
            # Calculate centers
            x_center = (x_range[0] + x_range[1]) / 2
            y_center = (y_range[0] + y_range[1]) / 2
            z_center = (z_range[0] + z_range[1]) / 2
            
            # Set limits with padding
            self.ax.set_xlim(x_center - max_range/2 - padding, x_center + max_range/2 + padding)
            self.ax.set_ylim(y_center - max_range/2 - padding, y_center + max_range/2 + padding)
            self.ax.set_zlim(z_center - max_range/2 - padding, z_center + max_range/2 + padding)
            
        # Add hard iron offset point if calibrated
        if self.cal.is_calibrated:
            # Check if offset point exists
            offset_point = None
            for artist in self.ax.get_children():
                if hasattr(artist, '_gid') and artist._gid == 'offset_point':
                    offset_point = artist
                    break
                    
            if offset_point is None:
                # Create offset point
                offset_point = self.ax.scatter(
                    [self.cal.offset_x], 
                    [self.cal.offset_y], 
                    [self.cal.offset_z],
                    color='red', 
                    marker='x', 
                    s=100,
                    label='Hard Iron Offset'
                )
                offset_point._gid = 'offset_point'
            else:
                # Update offset point
                offset_point._offsets3d = (
                    [self.cal.offset_x], 
                    [self.cal.offset_y], 
                    [self.cal.offset_z]
                )
                
        # Update title with point count
        self.ax.set_title(f'Magnetometer Calibration - {len(data)} points')
            
        return self.scatter,
        
    def start_animation(self, interval=50):
        """Start the animation."""
        if self.animation is not None:
            self.animation.event_source.stop()
            
        self.animation = FuncAnimation(
            self.fig, 
            self.update_plot, 
            interval=interval,
            blit=True
        )
        
        plt.tight_layout()
        plt.show()
        
    def create_interactive_dashboard(self):
        """Create an interactive dashboard for visualization and calibration."""
        # Create serial connection widgets
        port_text = widgets.Text(description='Port:', value='COM3', placeholder='e.g., COM3 or /dev/ttyUSB0')
        baud_dropdown = widgets.Dropdown(
            description='Baud Rate:',
            options=[9600, 19200, 38400, 57600, 115200, 230400],
            value=115200
        )
        format_dropdown = widgets.Dropdown(
            description='Format:',
            options=['json', 'csv'],
            value='json'
        )
        x_key = widgets.Text(description='X Key:', value='mx', placeholder='JSON key or CSV column')
        y_key = widgets.Text(description='Y Key:', value='my', placeholder='JSON key or CSV column')
        z_key = widgets.Text(description='Z Key:', value='mz', placeholder='JSON key or CSV column')
        
        # Buttons for serial control
        button_start = widgets.Button(description="Start Collection", 
                                    button_style='success',
                                    icon='play')
        button_stop = widgets.Button(description="Stop Collection", 
                                   button_style='danger',
                                   icon='stop')
                                   
        # Buttons for calibration
        button_calibrate = widgets.Button(description="Calibrate")
        button_reset = widgets.Button(description="Reset Data")
        button_save = widgets.Button(description="Save Calibration")
        button_load = widgets.Button(description="Load Calibration")
        
        # File path for saving/loading
        file_path = widgets.Text(description='File Path:', value='magnetometer_cal.npz')
        
        # Status indicators
        collecting_indicator = widgets.Valid(description='Collecting:', value=False)
        calibrated_indicator = widgets.Valid(description='Calibrated:', value=False)
        points_text = widgets.Label(value='0 points collected')
        quality_text = widgets.Label(value='Calibration Quality: N/A')
        
        # Output area for messages
        output = widgets.Output()
        
        # Update function for status
        def update_status():
            if hasattr(self.cal, 'is_collecting'):
                collecting_indicator.value = self.cal.is_collecting
            calibrated_indicator.value = self.cal.is_calibrated
            
            # Update point count
            if len(self.cal.data_buffer) > 0:
                points_text.value = f'{len(self.cal.data_buffer)} points collected'
            elif self.cal.raw_data is not None:
                points_text.value = f'{len(self.cal.raw_data)} points available'
            else:
                points_text.value = '0 points collected'
                
            # Update quality text
            if self.cal.is_calibrated:
                quality = self.cal.fit_quality
                quality_text.value = f'Calibration Quality: {quality:.2f} ({self.cal.get_fit_quality_description()})'
            else:
                quality_text.value = 'Calibration Quality: N/A'
        
        # Define button callbacks
        def on_start_clicked(b):
            with output:
                output.clear_output()
                success = self.cal.start_serial_collection(
                    port_text.value, 
                    baud_rate=baud_dropdown.value,
                    data_format=format_dropdown.value,
                    x_key=x_key.value,
                    y_key=y_key.value,
                    z_key=z_key.value
                )
                if success:
                    # Start animation if not already running
                    if not self.is_running:
                        self.is_running = True
                        self.start_animation()
                    
                    # Start a background thread to update status
                    def update_thread():
                        while self.cal.is_collecting:
                            update_status()
                            time.sleep(0.5)
                    
                    threading.Thread(target=update_thread, daemon=True).start()
                    
        def on_stop_clicked(b):
            with output:
                output.clear_output()
                self.cal.stop_serial_collection()
                update_status()
                
        def on_calibrate_clicked(b):
            with output:
                output.clear_output()
                
                # If we're collecting data but haven't stored it in raw_data yet
                if self.cal.raw_data is None and len(self.cal.data_buffer) > 0:
                    self.cal.raw_data = np.array(list(self.cal.data_buffer))
                    
                if self.cal.raw_data is None or len(self.cal.raw_data) < 10:
                    print("Not enough data points for calibration. Collect more data.")
                    return
                    
                success = self.cal.calibrate()
                if success:
                    print(f"Calibration successful!")
                    print(f"Fit quality: {self.cal.fit_quality:.2f} ({self.cal.get_fit_quality_description()})")
                    print(f"Hard iron offset: [{self.cal.offset_x:.6f}, {self.cal.offset_y:.6f}, {self.cal.offset_z:.6f}]")
                    
                    # Update status
                    update_status()
                else:
                    print("Calibration failed.")
                    
        def on_reset_clicked(b):
            with output:
                output.clear_output()
                self.cal.data_buffer.clear()
                self.cal.raw_data = None
                self.cal.calibrated_data = None
                self.cal.is_calibrated = False
                print("Data reset. Ready for new collection.")
                update_status()
                
        def on_save_clicked(b):
            with output:
                output.clear_output()
                if not self.cal.is_calibrated:
                    print("Cannot save: No calibration performed yet")
                    return
                    
                path = file_path.value
                if not path.endswith('.npz'):
                    path += '.npz'
                    
                success = self.cal.save_calibration(path)
                if success:
                    print(f"Calibration saved to {path}")
                    
        def on_load_clicked(b):
            with output:
                output.clear_output()
                path = file_path.value
                success = self.cal.load_calibration(path)
                if success:
                    print(f"Calibration loaded from {path}")
                    print(f"Hard iron offset: [{self.cal.offset_x:.6f}, {self.cal.offset_y:.6f}, {self.cal.offset_z:.6f}]")
                    update_status()
        
        # Connect callbacks
        button_start.on_click(on_start_clicked)
        button_stop.on_click(on_stop_clicked)
        button_calibrate.on_click(on_calibrate_clicked)
        button_reset.on_click(on_reset_clicked)
        button_save.on_click(on_save_clicked)
        button_load.on_click(on_load_clicked)
        
        # Create UI layout
        serial_box = widgets.VBox([
            widgets.HBox([port_text, baud_dropdown]),
            widgets.HBox([format_dropdown]),
            widgets.HBox([x_key, y_key, z_key]),
            widgets.HBox([button_start, button_stop])
        ])
        
        calibration_box = widgets.VBox([
            widgets.HBox([button_calibrate, button_reset]),
            widgets.HBox([button_save, button_load, file_path])
        ])
        
        status_box = widgets.VBox([
            widgets.HBox([collecting_indicator, calibrated_indicator]), 
            widgets.HBox([points_text]), 
            widgets.HBox([quality_text])
        ])
        
        # Create tabs
        tab = widgets.Tab()
        tab.children = [serial_box, calibration_box, status_box, output]
        tab.set_title(0, 'Connection')
        tab.set_title(1, 'Calibration')
        tab.set_title(2, 'Status')
        tab.set_title(3, 'Messages')
        
        # Main layout
        main_layout = widgets.VBox([
            widgets.HTML("<h2>Magnetometer Live Calibration</h2>"),
            tab
        ])
        
        # Display the UI
        display(main_layout)
        
        # Initialize status
        update_status()
        
        return self.fig
        

# Example usage
if __name__ == "__main__":
    visualizer = MagnetometerVisualizer()
    visualizer.create_interactive_dashboard()
