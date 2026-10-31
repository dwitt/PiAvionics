import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Ellipsoid
from mpl_toolkits.mplot3d import Axes3D
import ipywidgets as widgets
from IPython.display import display, clear_output
from scipy.optimize import minimize
import pandas as pd
from sklearn.preprocessing import StandardScaler
import serial
import threading
import time
import json
from collections import deque

class MagnetometerCalibration:
    """
    A class for calibrating magnetometer data similar to MotionCal.
    This performs hard iron and soft iron calibration with real-time
    serial port input capabilities.
    """
    
    def __init__(self):
        """Initialize the calibration parameters."""
        # Hard iron offsets (bias)
        self.offset_x = 0.0
        self.offset_y = 0.0
        self.offset_z = 0.0
        
        # Soft iron transformation matrix (initially identity)
        self.W = np.eye(3)
        
        # Reference magnetic field strength (can be adjusted based on location)
        self.field_reference = 1.0
        
        # Calibration parameters
        self.is_calibrated = False
        self.raw_data = None
        self.calibrated_data = None
        self.fit_quality = 0.0
        
        # Real-time data collection
        self.data_buffer = deque(maxlen=5000)  # Stores the most recent 5000 data points
        self.serial_port = None
        self.is_collecting = False
        self.collection_thread = None
        self.data_format = 'json'  # Default format (can be 'json', 'csv', or 'binary')

    def load_data_from_csv(self, file_path, x_col='mag_x', y_col='mag_y', z_col='mag_z'):
        """
        Load magnetometer data from CSV file.
        
        Parameters:
        ----------
        file_path : str
            Path to the CSV file
        x_col, y_col, z_col : str
            Column names for magnetometer x, y, z values
        """
        try:
            df = pd.read_csv(file_path)
            self.raw_data = df[[x_col, y_col, z_col]].values
            print(f"Loaded {len(self.raw_data)} data points from {file_path}")
            return True
        except Exception as e:
            print(f"Error loading data: {e}")
            return False

    def set_data(self, data):
        """
        Set magnetometer data directly from NumPy array.
        
        Parameters:
        ----------
        data : ndarray
            Nx3 array of magnetometer readings [x, y, z]
        """
        self.raw_data = np.array(data)
        print(f"Set {len(self.raw_data)} data points")
    
    def _objective_function(self, params):
        """
        Objective function for optimization.
        We want to minimize the variance of the magnitude of the calibrated field.
        
        Parameters:
        ----------
        params : array
            [offset_x, offset_y, offset_z, W_11, W_12, W_13, W_21, W_22, W_23, W_31, W_32, W_33]
        """
        if self.raw_data is None or len(self.raw_data) < 10:
            return np.inf
        
        # Extract parameters
        offset = params[0:3]
        W_flat = params[3:12]
        
        # Reconstruct the W matrix (only 9 parameters, assuming W is symmetric)
        W = np.array([
            [W_flat[0], W_flat[1], W_flat[2]],
            [W_flat[1], W_flat[3], W_flat[4]], 
            [W_flat[2], W_flat[4], W_flat[5]]
        ])
        
        # Apply calibration
        centered_data = self.raw_data - offset
        calibrated = np.dot(centered_data, W)
        
        # Calculate magnitudes
        magnitudes = np.linalg.norm(calibrated, axis=1)
        
        # We want these magnitudes to be as close to the reference field as possible
        # and with minimal variance
        mean_magnitude = np.mean(magnitudes)
        variance = np.var(magnitudes / mean_magnitude)
        
        # Also penalize if the mean magnitude is far from the reference field
        scale_penalty = (mean_magnitude - self.field_reference)**2
        
        return variance + 0.1 * scale_penalty
    
    def calibrate(self, max_iterations=1000):
        """
        Perform calibration using numerical optimization.
        
        Parameters:
        ----------
        max_iterations : int
            Maximum number of iterations for the optimizer
        """
        if self.raw_data is None or len(self.raw_data) < 10:
            print("Not enough data points for calibration")
            return False
        
        # Initial guess
        initial_offset = np.mean(self.raw_data, axis=0)
        initial_W = np.eye(3).flatten()[0:6]  # Only need 6 parameters for symmetric matrix
        initial_params = np.concatenate([initial_offset, initial_W])
        
        # Optimize
        result = minimize(
            self._objective_function, 
            initial_params,
            method='Powell',  # Powell's method works well for this problem
            options={'maxiter': max_iterations}
        )
        
        if result.success:
            # Extract the optimized parameters
            self.offset_x, self.offset_y, self.offset_z = result.x[0:3]
            W_flat = result.x[3:9]
            
            # Reconstruct the W matrix
            self.W = np.array([
                [W_flat[0], W_flat[1], W_flat[2]],
                [W_flat[1], W_flat[3], W_flat[4]], 
                [W_flat[2], W_flat[4], W_flat[5]]
            ])
            
            # Calculate the calibrated data
            self.calibrated_data = self._apply_calibration(self.raw_data)
            
            # Calculate fit quality metric (from 0.0 to 1.0)
            raw_magnitudes = np.linalg.norm(self.raw_data, axis=1)
            cal_magnitudes = np.linalg.norm(self.calibrated_data, axis=1)
            
            raw_cv = np.std(raw_magnitudes) / np.mean(raw_magnitudes)  # Coefficient of variation
            cal_cv = np.std(cal_magnitudes) / np.mean(cal_magnitudes)
            
            # Calculate improvement
            if raw_cv > 0:
                self.fit_quality = min(1.0, max(0.0, 1.0 - cal_cv/raw_cv))
            else:
                self.fit_quality = 0.0
                
            self.is_calibrated = True
            print(f"Calibration completed successfully. Fit quality: {self.fit_quality:.2f}")
            return True
        else:
            print("Calibration failed")
            return False
    
    def _apply_calibration(self, data):
        """Apply the calibration parameters to the given data."""
        offset = np.array([self.offset_x, self.offset_y, self.offset_z])
        centered_data = data - offset
        return np.dot(centered_data, self.W)
    
    def apply_calibration(self, data=None):
        """
        Apply calibration to data.
        
        Parameters:
        ----------
        data : ndarray or None
            If None, applies to the raw data used for calibration
            
        Returns:
        -------
        ndarray
            Calibrated data
        """
        if not self.is_calibrated:
            print("Calibration has not been performed yet")
            return None
        
        if data is None:
            if self.raw_data is None:
                print("No data available")
                return None
            data = self.raw_data
            
        return self._apply_calibration(data)
    
    def start_serial_collection(self, port, baud_rate=115200, data_format='json', 
                          hard_iron_offset=(0.5, -0.7, 0.2), 
                          soft_iron_scale=(1.2, 0.9, 1.1),
                          noise_level=0.05):
        """Generate synthetic magnetometer data for testing."""
        # Generate points on a unit sphere
        theta = np.random.uniform(0, 2*np.pi, num_points)
        phi = np.random.uniform(0, np.pi, num_points)
        
        x = np.sin(phi) * np.cos(theta)
        y = np.sin(phi) * np.sin(theta)
        z = np.cos(phi)
        
        # Apply soft iron distortion (scaling)
        x *= soft_iron_scale[0]
        y *= soft_iron_scale[1]
        z *= soft_iron_scale[2]
        
        # Apply hard iron offset
        x += hard_iron_offset[0]
        y += hard_iron_offset[1]
        z += hard_iron_offset[2]
        
        # Add noise
        x += np.random.normal(0, noise_level, num_points)
        y += np.random.normal(0, noise_level, num_points)
        z += np.random.normal(0, noise_level, num_points)
        
        return np.column_stack([x, y, z])
    
    # Example workflow for real-time serial collection
    def realtime_serial_workflow():
        """Example workflow for real-time serial collection"""
        # Create the calibration object
        cal = MagnetometerCalibration()
        
        # Create the interactive calibration widget
        cal.create_interactive_calibration()
        
        return cal
        
    # Example workflow with synthetic data
    def example_workflow():
        """Example workflow with synthetic data"""
        # Create the calibration object
        cal = MagnetometerCalibration()
        
        # Generate test data
        data = generate_test_data()
        cal.set_data(data)
        
        # Plot the raw data
        print("Plotting raw data...")
        cal.plot_realtime_view()
        
        # Perform calibration
        print("Performing calibration...")
        cal.calibrate()
        
        # Get calibration parameters
        params = cal.get_calibration_params()
        print("Calibration parameters:")
        for key, value in params.items():
            print(f"{key}: {value}")
        
        # Plot the results
        print("Plotting calibrated data...")
        cal.plot_3d()
        
        # Create interactive widget
        print("Creating interactive widget...")
        cal.create_interactive_calibration()
        
        return cal

    # Simulate serial port data for testing
    def simulate_serial_port():
        """
        Create a virtual serial port pair for testing.
        Requires the pyserial-simulator package.
        
        Returns:
        -------
        tuple
            (client_port, server_port) - port names that can be used for testing
        """
        try:
            from serial_simulator.simulator import SerialSimulator
            simulator = SerialSimulator()
            client_port, server_port = simulator.get_port_names()
            print(f"Created virtual serial ports: {client_port} and {server_port}")
            
            # Start sending simulated magnetometer data
            def send_data():
                server = serial.Serial(server_port, 115200)
                print(f"Sending simulated data on {server_port}")
                
                # Generate some test data
                data = generate_test_data(num_points=1000)
                
                # Send data points at regular intervals
                for point in data:
                    # JSON format
                    json_data = json.dumps({"mx": float(point[0]), "my": float(point[1]), "mz": float(point[2])})
                    server.write(f"{json_data}\n".encode('utf-8'))
                    time.sleep(0.05)  # 20 Hz
                    
                server.close()
                
            # Start sending data in a background thread
            threading.Thread(target=send_data, daemon=True).start()
            
            return client_port, server_port
        except ImportError:
            print("pyserial-simulator not found. Install with: pip install pyserial-simulator")
            return None, None
        except Exception as e:
            print(f"Error creating virtual serial ports: {e}")
            return None, None
            
    # Run the example workflow
    # For real hardware, use:
    # cal = realtime_serial_workflow()
    
    # For testing with generated data:
    cal = example_workflow()
    
    # For testing with simulated serial port:
    # client_port, _ = simulate_serial_port()
    # if client_port:
    #     cal = MagnetometerCalibration()
    #     cal.create_interactive_calibration()
    #     # You can then connect to the client_port in the UI   x_key='mx', y_key='my', z_key='mz', delimiter=','):
        """
        Start collecting magnetometer data from a serial port.
        
        Parameters:
        ----------
        port : str
            Serial port name (e.g., 'COM3' on Windows, '/dev/ttyUSB0' on Linux)
        baud_rate : int
            Baud rate for the serial connection
        data_format : str
            Format of incoming data: 'json', 'csv', or 'binary'
        x_key, y_key, z_key : str
            Keys for magnetometer data in JSON or column indices for CSV
        delimiter : str
            Delimiter for CSV data
        """
        if self.is_collecting:
            print("Already collecting data. Stop collection first.")
            return False
            
        try:
            self.serial_port = serial.Serial(port, baud_rate, timeout=1)
            self.is_collecting = True
            self.data_format = data_format
            self.data_keys = {'x': x_key, 'y': y_key, 'z': z_key}
            self.delimiter = delimiter
            
            # Clear the buffer
            self.data_buffer.clear()
            
            # Start collection thread
            self.collection_thread = threading.Thread(
                target=self._collect_serial_data, 
                daemon=True
            )
            self.collection_thread.start()
            
            print(f"Started data collection from {port} at {baud_rate} baud")
            return True
        except Exception as e:
            print(f"Error starting serial collection: {e}")
            self.is_collecting = False
            return False
            
    def stop_serial_collection(self):
        """Stop collecting data from the serial port."""
        if not self.is_collecting:
            print("Not currently collecting data")
            return
            
        self.is_collecting = False
        if self.collection_thread:
            self.collection_thread.join(timeout=1.0)
            
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            
        print("Stopped data collection")
        
        # Convert buffer to numpy array for calibration
        if len(self.data_buffer) > 0:
            self.raw_data = np.array(list(self.data_buffer))
            print(f"Collected {len(self.raw_data)} data points")
            
    def _collect_serial_data(self):
        """Thread function to collect data from serial port."""
        while self.is_collecting and self.serial_port and self.serial_port.is_open:
            try:
                if self.serial_port.in_waiting > 0:
                    line = self.serial_port.readline().decode('utf-8').strip()
                    
                    # Parse data based on format
                    data_point = self._parse_serial_data(line)
                    
                    if data_point is not None:
                        self.data_buffer.append(data_point)
                else:
                    time.sleep(0.01)  # Small delay to prevent CPU hogging
            except Exception as e:
                print(f"Error reading serial data: {e}")
                time.sleep(0.1)
                
    def _parse_serial_data(self, line):
        """
        Parse a line of serial data based on the specified format.
        
        Parameters:
        ----------
        line : str
            Line of data from the serial port
            
        Returns:
        -------
        list or None
            [x, y, z] magnetometer values or None if parsing failed
        """
        try:
            if self.data_format == 'json':
                # Parse JSON format
                data = json.loads(line)
                x = float(data[self.data_keys['x']])
                y = float(data[self.data_keys['y']])
                z = float(data[self.data_keys['z']])
                return [x, y, z]
                
            elif self.data_format == 'csv':
                # Parse CSV format
                values = line.split(self.delimiter)
                
                # If keys are strings, assume they're column names in a header row
                if isinstance(self.data_keys['x'], str) and self.data_keys['x'].isalpha():
                    # First row might be header - skip it
                    if any(k in values for k in self.data_keys.values()):
                        return None
                        
                    # Try to parse as indices
                    try:
                        x_idx = int(self.data_keys['x'])
                        y_idx = int(self.data_keys['y'])
                        z_idx = int(self.data_keys['z'])
                    except ValueError:
                        # Default to first three columns
                        x_idx, y_idx, z_idx = 0, 1, 2
                        
                    x = float(values[x_idx])
                    y = float(values[y_idx])
                    z = float(values[z_idx])
                else:
                    # Keys are indices
                    x = float(values[int(self.data_keys['x'])])
                    y = float(values[int(self.data_keys['y'])])
                    z = float(values[int(self.data_keys['z'])])
                    
                return [x, y, z]
                
            elif self.data_format == 'binary':
                # Binary format - assuming 3 floats (12 bytes)
                # This would need to be implemented based on your specific binary format
                pass
                
            return None
        except Exception as e:
            # Silently ignore parsing errors - they're common in serial data
            return None

    def start_realtime_visualization(self, update_interval=100):
        """
        Start a real-time visualization of the incoming magnetometer data.
        
        Parameters:
        ----------
        update_interval : int
            Update interval in milliseconds
        """
        if not self.is_collecting:
            print("Not collecting data. Start collection first.")
            return
            
        # Create figure
        fig = plt.figure(figsize=(12, 10))
        ax = fig.add_subplot(111, projection='3d')
        
        # Create empty scatter plot
        scatter = ax.scatter([], [], [], c='b', marker='o', alpha=0.3)
        
        # Set labels
        ax.set_xlabel('X')
        ax.set_ylabel('Y')
        ax.set_zlabel('Z')
        ax.set_title('Real-time Magnetometer Data')
        
        # Create animation function
        def update(_):
            if not self.is_collecting or len(self.data_buffer) == 0:
                return scatter,
                
            # Get data from buffer
            data = np.array(list(self.data_buffer))
            
            # Update scatter plot
            scatter._offsets3d = (data[:, 0], data[:, 1], data[:, 2])
            
            # Update plot limits
            ax.set_xlim(np.min(data[:, 0]), np.max(data[:, 0]))
            ax.set_ylim(np.min(data[:, 1]), np.max(data[:, 1]))
            ax.set_zlim(np.min(data[:, 2]), np.max(data[:, 2]))
            
            return scatter,
            
        # Create animation
        from matplotlib.animation import FuncAnimation
        anim = FuncAnimation(fig, update, interval=update_interval, blit=True)
        
        plt.show()
        
    def plot_3d(self, figsize=(12, 10), show_raw=True, show_calibrated=True):
        """
        Plot the raw and calibrated data in 3D.
        
        Parameters:
        ----------
        figsize : tuple
            Figure size (width, height)
        show_raw : bool
            Whether to show raw data
        show_calibrated : bool
            Whether to show calibrated data
        """
        if self.raw_data is None and len(self.data_buffer) == 0:
            print("No data available")
            return
            
        # If we're collecting data but haven't stored it in raw_data yet
        if self.raw_data is None and len(self.data_buffer) > 0:
            data = np.array(list(self.data_buffer))
        else:
            data = self.raw_data
        
        fig = plt.figure(figsize=figsize)
        ax = fig.add_subplot(111, projection='3d')
        
        # Plot raw data
        if show_raw:
            ax.scatter(
                data[:, 0], 
                data[:, 1], 
                data[:, 2], 
                c='b', marker='o', alpha=0.3, label='Raw'
            )
            
        # Plot calibrated data if available
        if show_calibrated and self.is_calibrated and self.calibrated_data is not None:
            ax.scatter(
                self.calibrated_data[:, 0], 
                self.calibrated_data[:, 1], 
                self.calibrated_data[:, 2], 
                c='r', marker='.', alpha=0.5, label='Calibrated'
            )
        
        # Plot origin and offset
        ax.scatter([0], [0], [0], c='k', marker='+', s=100, label='Origin')
        
        if self.is_calibrated:
            ax.scatter(
                [self.offset_x], 
                [self.offset_y], 
                [self.offset_z], 
                c='g', marker='x', s=100, label='Hard Iron Offset'
            )
            
        # Set labels and legend
        ax.set_xlabel('X')
        ax.set_ylabel('Y')
        ax.set_zlabel('Z')
        ax.legend()
        
        # Try to set equal aspect ratio
        max_range = np.array([
            ax.get_xlim()[1] - ax.get_xlim()[0],
            ax.get_ylim()[1] - ax.get_ylim()[0],
            ax.get_zlim()[1] - ax.get_zlim()[0]
        ]).max() / 2.0
        
        mid_x = (ax.get_xlim()[1] + ax.get_xlim()[0]) / 2
        mid_y = (ax.get_ylim()[1] + ax.get_ylim()[0]) / 2
        mid_z = (ax.get_zlim()[1] + ax.get_zlim()[0]) / 2
        
        ax.set_xlim(mid_x - max_range, mid_x + max_range)
        ax.set_ylim(mid_y - max_range, mid_y + max_range)
        ax.set_zlim(mid_z - max_range, mid_z + max_range)
        
        plt.title("Magnetometer Calibration")
        plt.tight_layout()
        plt.show()
        
    def plot_realtime_view(self, data=None, figsize=(15, 5)):
        """
        Plot a real-time view of the data similar to MotionCal.
        
        Parameters:
        ----------
        data : ndarray or None
            If None, uses the raw data
        figsize : tuple
            Figure size (width, height)
        """
        if data is None:
            if self.raw_data is None:
                print("No data available")
                return
            data = self.raw_data
            
        # Create figure with subplots
        fig, axes = plt.subplots(1, 3, figsize=figsize)
        
        # Plot XY plane
        axes[0].scatter(data[:, 0], data[:, 1], alpha=0.3, s=10)
        axes[0].set_title('XY Plane')
        axes[0].set_xlabel('X')
        axes[0].set_ylabel('Y')
        axes[0].grid(True)
        
        # Plot XZ plane
        axes[1].scatter(data[:, 0], data[:, 2], alpha=0.3, s=10)
        axes[1].set_title('XZ Plane')
        axes[1].set_xlabel('X')
        axes[1].set_ylabel('Z')
        axes[1].grid(True)
        
        # Plot YZ plane
        axes[2].scatter(data[:, 1], data[:, 2], alpha=0.3, s=10)
        axes[2].set_title('YZ Plane')
        axes[2].set_xlabel('Y')
        axes[2].set_ylabel('Z')
        axes[2].grid(True)
        
        # Try to set equal aspect ratio
        for ax in axes:
            ax.set_aspect('equal')
            
        plt.tight_layout()
        plt.show()
        
    def get_calibration_params(self):
        """
        Get the calibration parameters.
        
        Returns:
        -------
        dict
            Calibration parameters
        """
        if not self.is_calibrated:
            print("Calibration has not been performed yet")
            return None
            
        return {
            'hard_iron_offset': {
                'x': self.offset_x,
                'y': self.offset_y,
                'z': self.offset_z
            },
            'soft_iron_matrix': self.W.tolist(),
            'fit_quality': self.fit_quality
        }
        
    def save_calibration(self, file_path):
        """
        Save calibration parameters to a file.
        
        Parameters:
        ----------
        file_path : str
            Path to save the calibration parameters
        """
        if not self.is_calibrated:
            print("Calibration has not been performed yet")
            return False
            
        params = self.get_calibration_params()
        
        try:
            np.savez(
                file_path,
                hard_iron_offset=np.array([self.offset_x, self.offset_y, self.offset_z]),
                soft_iron_matrix=self.W,
                fit_quality=self.fit_quality
            )
            print(f"Calibration parameters saved to {file_path}")
            return True
        except Exception as e:
            print(f"Error saving calibration parameters: {e}")
            return False
            
    def load_calibration(self, file_path):
        """
        Load calibration parameters from a file.
        
        Parameters:
        ----------
        file_path : str
            Path to the calibration parameters file
        """
        try:
            data = np.load(file_path)
            self.offset_x, self.offset_y, self.offset_z = data['hard_iron_offset']
            self.W = data['soft_iron_matrix']
            self.fit_quality = float(data['fit_quality'])
            self.is_calibrated = True
            
            # If we have raw data, apply calibration
            if self.raw_data is not None:
                self.calibrated_data = self._apply_calibration(self.raw_data)
                
            print(f"Calibration parameters loaded from {file_path}")
            return True
        except Exception as e:
            print(f"Error loading calibration parameters: {e}")
            return False
            
    def get_fit_quality_description(self):
        """Get a description of the fit quality."""
        if not self.is_calibrated:
            return "Not calibrated"
            
        if self.fit_quality > 0.95:
            return "Excellent"
        elif self.fit_quality > 0.85:
            return "Good"
        elif self.fit_quality > 0.7:
            return "Fair"
        elif self.fit_quality > 0.5:
            return "Poor"
        else:
            return "Very poor"
    
    def create_interactive_calibration(self):
        """Create an interactive widget for calibration with serial port support."""
        # Create widgets for serial connection
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
        button_visualize = widgets.Button(description="Live Visualization", 
                                        button_style='info',
                                        icon='eye')
        
        # Buttons for calibration
        button_calibrate = widgets.Button(description="Calibrate")
        button_plot_raw = widgets.Button(description="Plot Raw Data")
        button_plot_calibrated = widgets.Button(description="Plot Calibrated")
        button_save = widgets.Button(description="Save Calibration")
        button_load = widgets.Button(description="Load Calibration")
        
        # File path for saving/loading
        file_path = widgets.Text(description='File Path:', value='magnetometer_cal.npz')
        
        # Status indicators
        collecting_indicator = widgets.Valid(description='Collecting:', value=False)
        calibrated_indicator = widgets.Valid(description='Calibrated:', value=False)
        points_text = widgets.Label(value='0 points collected')
        
        # Output area
        output = widgets.Output()
        
        # Update function for status
        def update_status():
            if hasattr(self, 'is_collecting'):
                collecting_indicator.value = self.is_collecting
            calibrated_indicator.value = self.is_calibrated
            
            # Update point count
            if len(self.data_buffer) > 0:
                points_text.value = f'{len(self.data_buffer)} points collected'
            elif self.raw_data is not None:
                points_text.value = f'{len(self.raw_data)} points available'
            else:
                points_text.value = '0 points collected'
        
        # Define button callbacks
        def on_start_clicked(b):
            with output:
                output.clear_output()
                success = self.start_serial_collection(
                    port_text.value, 
                    baud_rate=baud_dropdown.value,
                    data_format=format_dropdown.value,
                    x_key=x_key.value,
                    y_key=y_key.value,
                    z_key=z_key.value
                )
                if success:
                    # Start a background thread to update status
                    def update_thread():
                        while self.is_collecting:
                            update_status()
                            time.sleep(0.5)
                    
                    threading.Thread(target=update_thread, daemon=True).start()
                    
        def on_stop_clicked(b):
            with output:
                output.clear_output()
                self.stop_serial_collection()
                update_status()
                
        def on_visualize_clicked(b):
            with output:
                output.clear_output()
                if self.is_collecting:
                    print("Starting real-time visualization. Close the plot window to return to this interface.")
                    self.start_realtime_visualization()
                else:
                    print("Not collecting data. Start collection first.")
                
        def on_calibrate_clicked(b):
            with output:
                output.clear_output()
                
                # If we're collecting data but haven't stored it in raw_data yet
                if self.raw_data is None and len(self.data_buffer) > 0:
                    self.raw_data = np.array(list(self.data_buffer))
                    
                if self.raw_data is None or len(self.raw_data) < 10:
                    print("Not enough data points for calibration. Collect more data.")
                    return
                    
                self.calibrate()
                print(f"Fit quality: {self.fit_quality:.2f} ({self.get_fit_quality_description()})")
                print(f"Hard iron offset: [{self.offset_x:.6f}, {self.offset_y:.6f}, {self.offset_z:.6f}]")
                
                # Update status
                update_status()
                
        def on_plot_raw_clicked(b):
            with output:
                output.clear_output()
                
                # Use data buffer if raw_data is not available
                if self.raw_data is None and len(self.data_buffer) > 0:
                    data = np.array(list(self.data_buffer))
                    self.plot_realtime_view(data)
                elif self.raw_data is not None:
                    self.plot_realtime_view(self.raw_data)
                else:
                    print("No data available")
                
        def on_plot_calibrated_clicked(b):
            with output:
                output.clear_output()
                if self.is_calibrated:
                    self.plot_realtime_view(self.calibrated_data)
                    self.plot_3d()
                else:
                    print("Please calibrate first")
                    
        def on_save_clicked(b):
            with output:
                output.clear_output()
                if not self.is_calibrated:
                    print("Cannot save: No calibration performed yet")
                    return
                    
                path = file_path.value
                if not path.endswith('.npz'):
                    path += '.npz'
                    
                success = self.save_calibration(path)
                if success:
                    print(f"Calibration saved to {path}")
                    
        def on_load_clicked(b):
            with output:
                output.clear_output()
                path = file_path.value
                success = self.load_calibration(path)
                if success:
                    print(f"Calibration loaded from {path}")
                    print(f"Hard iron offset: [{self.offset_x:.6f}, {self.offset_y:.6f}, {self.offset_z:.6f}]")
                    update_status()
        
        # Connect callbacks
        button_start.on_click(on_start_clicked)
        button_stop.on_click(on_stop_clicked)
        button_visualize.on_click(on_visualize_clicked)
        button_calibrate.on_click(on_calibrate_clicked)
        button_plot_raw.on_click(on_plot_raw_clicked)
        button_plot_calibrated.on_click(on_plot_calibrated_clicked)
        button_save.on_click(on_save_clicked)
        button_load.on_click(on_load_clicked)
        
        # Create UI layout
        serial_box = widgets.VBox([
            widgets.HBox([port_text, baud_dropdown]),
            widgets.HBox([format_dropdown]),
            widgets.HBox([x_key, y_key, z_key]),
            widgets.HBox([button_start, button_stop, button_visualize])
        ])
        
        calibration_box = widgets.VBox([
            widgets.HBox([button_calibrate, button_plot_raw, button_plot_calibrated]),
            widgets.HBox([button_save, button_load, file_path])
        ])
        
        status_box = widgets.HBox([collecting_indicator, calibrated_indicator, points_text])
        
        # Main layout
        main_layout = widgets.VBox([
            widgets.HTML("<h2>Magnetometer Calibration</h2>"),
            widgets.HTML("<h3>Serial Connection</h3>"),
            serial_box,
            widgets.HTML("<h3>Calibration Controls</h3>"),
            calibration_box,
            widgets.HTML("<h3>Status</h3>"),
            status_box,
            widgets.HTML("<h3>Output</h3>"),
            output
        ])
        
        # Display the UI
        display(main_layout)
        
        # Initialize status
        update_status()


# Example usage
if __name__ == "__main__":
    # Generate synthetic data for testing
    def generate_test_data(num_points=1000, 
                          
