import requests
import json
import re
import time

# Replace 'your_github_token' with your GitHub Personal Access Token
github_token = "your_github_token"
headers = {
    "Authorization": f"token {github_token}",
    "Accept": "application/vnd.github.v3.text-match+json",
}


def search_code(query, language, per_page=10, page=1):
    """Search for code on GitHub containing the specified query in a specific language."""
    url = f"https://api.github.com/search/code?q={query}+language:{language}&per_page={per_page}&page={page}"
    response = requests.get(url, headers=headers)
    if response.status_code == 200:
        with open(f"resp_{page}_{per_page}.json", "w") as fp:
            fp.write(str(response.json()))
        return response.json()
    elif response.status_code == 403:
        time.sleep(62)
        return search_code(query, language, per_page, page)
    else:
        raise Exception(f"GitHub API error: {response.status_code}, {response.text}")


def count_prefetch_arguments(snippet):
    """Count occurrences of the third argument values in __builtin_prefetch calls.
    Handles both direct calls and macro definitions."""
    # Updated regex to better handle macro definitions and parentheses
    b_string = "__builtin_prefetch("
    counter = {}
    for line in snippet.split("\n"):
        while "__builtin_prefetch(" in line:
            line = line[line.find(b_string) + len(b_string) :]
            i = 1
            brackets = 1
            args = ""
            for c in line:
                if c == "(":
                    brackets += 1
                elif c == ")":
                    brackets -= 1
                i += 1
                if brackets == 1:
                    args += c
                if brackets == 0:
                    break
            args = args.split(",")
            if len(args) == 3:
                counter[args[2].strip()] = counter.get(args[2].strip(), 0) + 1
            else:
                counter["default"] = counter.get("default", 0) + 1
    return counter


def fetch_builtin_prefetch(per_page=10, max_pages=5):
    """Fetch code lines containing '__builtin_prefetch' from GitHub in C++."""
    query = "__builtin_prefetch"
    languages = ["C++"]
    all_results = []
    argument_counts = {}

    for language in languages:
        for page in range(1, max_pages + 1):
            # print(f"Fetching page {page} for language {language}...")
            result = search_code(query, language, per_page=per_page, page=page)

            for item in result.get("items", []):
                file_url = item["html_url"]
                repository = item["repository"]["full_name"]
                text_matches = item.get("text_matches", [])

                for match in text_matches:
                    snippet = match.get("fragment", "")
                    snippet_counts = count_prefetch_arguments(snippet)

                    # Update overall counts
                    for key in snippet_counts:
                        argument_counts[key] = (
                            argument_counts.get(key, 0) + snippet_counts[key]
                        )

                    all_results.append(
                        {
                            "repository": repository,
                            "file_url": file_url,
                            "language": language,
                            "snippet": snippet,
                            "counts": snippet_counts,
                        }
                    )
            print(argument_counts)
    print("\nOverall Argument Counts:", argument_counts)
    return all_results, argument_counts


def main():
    global_count = {}
    try:
        results, argument_counts = fetch_builtin_prefetch(
            per_page=100, max_pages=100
        )  # Limit to 2 pages for demonstration
        for key in argument_counts:
            global_count[key] = global_count.get(key, 0) + argument_counts[key]
        print("\nFinal Argument Counts:", argument_counts)
    except Exception as e:
        print(f"Error: {e}")
    print(global_count)


if __name__ == "__main__":
    main()
