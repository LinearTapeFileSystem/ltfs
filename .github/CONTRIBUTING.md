# Contributing to LTFS

We love pull requests from everyone.

When contributing to this repository, please first discuss the change you wish to make via issue, email, or any other method with the owners of this repository before making a change.

Please note we have a [coding style guide](../docs/CODE_OF_CONDUCT.md), please follow it in all your interactions with the project.

1. Fork, then clone the repo

  ```
  git clone git@github.com:your-username/ltfs.git
  ```

2. Make your fix or change

  * Please follow the style guidelines of this project
  * Please comment to the code, particularly in hard-to-understand areas
  * Please remove all warnings in the first target environment (RHEL7 x86_64)
  * Please confrim your fix or change works as expected

3. Push to your fork and submit pull request.

4. At this point you're waiting until we may suggest some changes or improvements or alternatives.

__Some things that will increase the chance that your pull request is accepted__

* Follow our [coding style guide](../docs/CODE_OF_CONDUCT.md).
* Write a good commit message.

# Branch Naming Conventions
The structure and design conventions are based mainly on the “Conventional Branch” concept and the team’s current work process.

## Purpose
1.	Purpose-driven Branch Names: Each branch name clearly indicates its purpose, making it easy for all developers to understand what the branch is for.
2.	Integration with CI/CD: By using consistent branch names, it can help automated systems (like Continuous Integration/Continuous Deployment pipelines) to trigger specific actions based on the branch type.
3.	Better type and version control: Branches are related to a specific type of change in the code and the version that is being worked on, allowing for a much better backtracking.

## Basic Rules
- Naming structure: 
`<type>/<description>`
- Naming prefixes (Types):
  - feature/: For new features (e.g., feature/add-login-page).
  - fix/: For general fixes or code correcting.
  - update/: For code upgrade or non-breaking code refactoring.
  - chore/: For non-code tasks like dependency, docs updates (e.g., chore/update-dependencies).
  - release/: For branches preparing a release (e.g., release/v1.2.0).
- Use Lowercase Alphanumeric and Hyphens: Always use lowercase letters (a-z), numbers (0-9), and hyphens to separate words. Avoid special characters, underscores, or spaces.
- No Consecutive or Trailing Hyphens: Ensure that hyphens are used singly, with no consecutive hyphens (feature/new--login) or at the end (feature/new-login-).
- Keep It Clear and Concise: The branch name should be descriptive yet concise, clearly indicating the purpose of the work.
- Include Ticket Numbers: If applicable, include the ticket number from your project management tool to make tracking easier. For example, for a ticket issue-123, the branch name could be feature/issue-123-new-login.

## General Project Structure
```
main
├── release/v1.x.x
│   ├── feature/lorem-ipsum
│   └── fix/ipsum-dolor
├── release/v2.x.x
│   └── chore/dolor-sit
└── release/v3.x.x
    ├── update/sit-amet
    ├── feature/amet-consectetur
    └── chore/consectetur-adipiscing
```
- main: This branch should only contain working versions completely tested and only receive pull requests from finalized release branches. All new release/ branches must come from this one.
- release: The creation of these branches should be done from the last version of main when created. Only this branches can make pull requests to main.
- Other branches: All working branches should come from a release branch. These are the only ones that should receive direct commits from changes made by development staff. These branches should only do PR to the active release branch.

# Commits Naming Conventions
The structure and design conventions are based mainly on the “Conventional Branch” concept and the team’s current work process.

## Purpose
1.	Automatically generating CHANGELOGs: When the commits are concise, and precise changelogs can be automated or semi-automated from the commit history.
2.	Communicating the nature of changes to everyone: Being able to understand the type of changing by just looking at a name and understanding the basics of a commit just by the title is useful in analysis and backtrack situations.
3.	Building code habits and good practices: Maintaining structured commits also leads to more precise coding practices and coherent code changes.

## Basic Rules
- Naming structure: 
```
<type>: <description>

[optional body]
```
- Naming prefixes (Types):
  - fix: A commit of the type fix patches a bug in your codebase.
  - feat: A commit of the type feat introduces a new feature to the codebase
  - <type>!: A commit that appends a ! after the type introduces a breaking change. A breaking change can be part of commits of any type.
  - update: A commit of type update changes existing code or refactors functions without changing functionality of adding features in your codebase.
  - build: A commit of type build changes build process in your codebase.
  - docs: A commit of type docs is for creation or updating documentation in your codebase.
  - chore: A commit of type chore is for every other minor task in your codebase.
- Commits must be prefixed with a type, which consists of a noun, feat, fix, etc., followed by the optional !, and required terminal colon and space.
- A description must immediately follow the colon and space after the type prefix. The description is a short summary of the code changes, e.g., fix: array parsing issue when multiple spaces were contained in string.
- A longer commit body may be provided after the short description, providing additional contextual information about the code changes. The body must begin one blank line after the description.
- A commit body is free-form and may consist of any number of newline separated paragraphs.

# Bibliography
[1]	“Conventional Branch,” Conventional Branch, 2025. https://conventional-branch.github.io/ (accessed 2025).</br>
[2]	“Conventional Commits,” Conventional Commits. https://www.conventionalcommits.org/en/v1.0.0/ (accessed 2025).
