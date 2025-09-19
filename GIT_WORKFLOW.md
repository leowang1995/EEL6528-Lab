# Git Workflow Guide - EEL6528 Lab 1

## Complete Step-by-Step Commands for GitHub Management

### Initial Setup (One-time only)

#### 1. Configure Git (if not done before)
```bash
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"
```

#### 2. Check if repository is already connected
```bash
git remote -v
```
Should show:
```
origin  https://github.com/leowang1995/EEL6528-Lab1.git (fetch)
origin  https://github.com/leowang1995/EEL6528-Lab1.git (push)
```

---

## Daily Workflow - Making Changes and Committing

### Step 1: Check Current Status
```bash
# See what files have changed
git status

# See detailed changes in files
git diff
```

### Step 2: Stage Files for Commit
```bash
# Add all changed files
git add .

# Or add specific files only
git add filename.cpp
git add another_file.h

# Or add multiple specific files
git add file1.cpp file2.h file3.txt
```

### Step 3: Review What's Staged
```bash
# Check what's ready to commit
git status

# See staged changes
git diff --staged
```

### Step 4: Commit Changes
```bash
# Commit with a descriptive message
git commit -m "Brief description of what you changed"

# For more detailed commits (opens text editor)
git commit

# Example commit messages:
git commit -m "Fix overflow detection in RX streamer"
git commit -m "Add support for higher sampling rates"
git commit -m "Update N210 configuration parameters"
git commit -m "Improve thread synchronization"
```

### Step 5: Push to GitHub
```bash
# Push to main branch
git push origin main

# Or simply (if main is default)
git push
```

---

## Complete Example Workflow

### Scenario: You modified lab1.cpp and added a new test file

```bash
# 1. Check what changed
git status
# Output: modified: lab1.cpp, untracked: test_new_feature.cpp

# 2. See the actual changes
git diff lab1.cpp

# 3. Add files to staging
git add lab1.cpp test_new_feature.cpp

# 4. Check staged files
git status
# Output: Changes to be committed: modified: lab1.cpp, new file: test_new_feature.cpp

# 5. Commit with message
git commit -m "Add new test feature and improve main algorithm

- Modified lab1.cpp to handle edge cases better
- Added test_new_feature.cpp for validation
- Fixed memory leak in sample processing"

# 6. Push to GitHub
git push origin main
```

---

## Advanced Git Commands

### View Commit History
```bash
# See recent commits
git log --oneline

# See detailed commit history
git log

# See changes in each commit
git log -p
```

### Undo Changes (Before Committing)
```bash
# Undo changes to a specific file
git checkout -- filename.cpp

# Undo all changes
git checkout -- .

# Remove file from staging (but keep changes)
git reset HEAD filename.cpp
```

### Update from GitHub (Pull Changes)
```bash
# Get latest changes from GitHub
git pull origin main

# Or simply
git pull
```

---

## Best Practices for Commit Messages

### Good Commit Messages:
```bash
git commit -m "Fix buffer overflow in sample processing"
git commit -m "Add N210 network configuration validation"
git commit -m "Improve error handling for hardware timeouts"
git commit -m "Update documentation for Linux installation"
git commit -m "Optimize thread pool performance"
```

### Multi-line Commit Messages:
```bash
git commit -m "Add comprehensive error handling

- Handle N210 connection timeouts gracefully
- Add retry logic for network failures
- Improve user error messages
- Add logging for debugging purposes"
```

---

## Quick Reference Commands

### Daily Commands (Most Used)
```bash
git status                    # Check what's changed
git add .                     # Stage all changes
git commit -m "message"       # Commit changes
git push                      # Upload to GitHub
git pull                      # Download from GitHub
```

### File Management
```bash
git add filename.cpp          # Stage specific file
git add *.cpp                 # Stage all .cpp files
git add folder/               # Stage entire folder
git reset HEAD filename.cpp   # Unstage file
```

### Information Commands
```bash
git log --oneline            # See commit history
git diff                     # See unstaged changes
git diff --staged            # See staged changes
git remote -v                # See remote repositories
```

---

## Windows PowerShell Workflow

### Complete Windows Example:
```powershell
# Navigate to your project
cd "C:\EEL6528 Lab\Lab 1"

# Check status
git status

# Add all changes
git add .

# Commit with message
git commit -m "Update lab implementation for better performance"

# Push to GitHub
git push origin main
```

---

## Linux Workflow

### Complete Linux Example:
```bash
# Navigate to project
cd ~/EEL6528-Lab1

# Check status
git status

# Add changes
git add .

# Commit
git commit -m "Test N210 with different sampling rates"

# Push to GitHub
git push origin main
```

---

## Troubleshooting Common Issues

### Problem: "Please tell me who you are" error
```bash
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"
```

### Problem: Authentication required
```bash
# Use personal access token instead of password
# Generate token at: https://github.com/settings/tokens
```

### Problem: Merge conflicts
```bash
# Pull latest changes first
git pull origin main

# Resolve conflicts in files, then:
git add .
git commit -m "Resolve merge conflicts"
git push origin main
```

### Problem: Forgot to pull before making changes
```bash
# Stash your changes
git stash

# Pull latest
git pull origin main

# Apply your changes back
git stash pop

# Then commit and push as normal
```

---

## Automated Backup Script

### Create a backup script (backup.bat for Windows):
```batch
@echo off
echo Backing up code to GitHub...
git add .
git commit -m "Auto-backup: %date% %time%"
git push origin main
echo Backup complete!
pause
```

### Or backup script for Linux (backup.sh):
```bash
#!/bin/bash
echo "Backing up code to GitHub..."
git add .
git commit -m "Auto-backup: $(date)"
git push origin main
echo "Backup complete!"
```

---

## Summary: Essential Daily Commands

1. **Check status**: `git status`
2. **Stage changes**: `git add .`
3. **Commit**: `git commit -m "Your message"`
4. **Push**: `git push origin main`
5. **Pull updates**: `git pull origin main`

**Remember**: Always commit frequently with meaningful messages. Each commit should represent a logical unit of work!
