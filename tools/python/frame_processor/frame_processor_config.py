import sys
import argparse
import ConfigParser
        
class FrameProcessorConfig(object):
    
    def __init__(self, name=sys.argv[0], description="No description"):

        defaults = {}
        defaults['ctrl_endpoint']    = "tcp://127.0.0.1:5000"
        defaults['ready_endpoint']   = "tcp://127.0.0.1:5001"
        defaults['release_endpoint'] = "tcp://127.0.0.1:5002"
        defaults['sharedbuf']        = "FrameReceiverBuffer"
        defaults['bypass_mode']      = False
        defaults['frames']       = 0

        # Parse the command-line argument list        
        arg_config = self._parse_arguments(name, description)
        
        # Parse the configuration file if specified
        file_config = {}
        if arg_config['config_file']:
            file_config = self._parse_file(arg_config['config_file'])
        
        # Merge the command-line, config file and defaults, with command-line args
        # taking highest priority
        for key in defaults:
            setattr(self, key, defaults[key])
            if key in file_config and file_config[key]:
                setattr(self, key, file_config[key])
            if key in arg_config and arg_config[key]:
                setattr(self, key, arg_config[key])
            
    def _parse_arguments(self, name, description):
        
        parser = argparse.ArgumentParser(prog=name, description=description)
        
        parser.add_argument('--config', '-c', type=str, default=None, dest='config_file',
                            help="Parse additional options from specified configuration file")
        parser.add_argument('--ctrl', type=str, default=None, dest='ctrl_endpoint',
                            help='Specify the IPC control channel endpoint URL')
        parser.add_argument('--sharedbuf', type=str, default=None, dest='sharedbuf',
                            help='Specify the name of the shared memory frame buffer')
        parser.add_argument('--bypass_mode', action="store_true",
                            help="Enable frame decoding bypass mode" )
        parser.add_argument('--frames', type=int, default=None, dest='frames',
                            help="Specify the number of frames to receive before shutting down")
        
        args = parser.parse_args()
        
        return vars(args)
        
        
    def _parse_file(self, config_file):
        
        config_vals = {}
        
        sec_name='FrameProcessor'
        cp = ConfigParser.ConfigParser()
        cp.read(config_file)
        
        config_vals['ctrl_endpoint'] = cp.get(sec_name, 'ctrl_endpoint')
#        config_vals['ready_endpoint'] = cp.get(sec_name, 'ready_endpoint')
#        config_vals['release_endpoint'] = cp.get(sec_name, 'release_endpoint')
        config_vals['sharedbuf'] = cp.get(sec_name, 'sharedbuf')
        config_vals['bypass_mode'] = cp.getboolean(sec_name, 'bypass_mode')
 #       config_vals['frames'] = cp.getint(sec_name, 'frames')
        
        return config_vals