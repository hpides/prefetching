## Fetch Me If You Can: Evaluating CPU Cache Prefetching and Its Reliability on High Latency Memory

This repository contains the code to our [DaMoN 2025 paper](https://hpi.de/oldsite/fileadmin/user_upload/fachgebiete/rabl/publications/2025/Mahling-DaMoN25-Prefetching.pdf).
The repository contains (Micro-)benchmarks for evaluation of prefetching algorithms on heterogeneous memory.

## Cite our work

If you use the microbenchmarks or reference our findings, please cite us.
 
```bibtex
@inproceedings{mahling2025prefetching,
  title={Fetch Me If You Can: Evaluating CPU Cache Prefetching and Its Reliability on High Latency Memory},
  author={Mahling, Fabian and Weisgut, Marcel and Rabl, Tilmann},
  booktitle={Proceedings of the International Workshop on Data Management on New Hardware},
  year={2025}
}
```

# Setup

- Run `setup.sh` to install all dependencies.
- Create a symlink from `/prefetching` to this folder or change the `default_repository_path` in `config.hpp`
- Create corresponding entries in `src/config.hpp`

# Note: This is just the initial Code dump. Clean-up and documentation are TBD.
