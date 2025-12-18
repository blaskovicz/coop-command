#ifndef DATE_FORMAT_H
#define DATE_FORMAT_H

// Format milliseconds to relative time string (e.g., "2d 3h 15m 42s")
String formatMillisToRelativeTime(unsigned long milliseconds)
{
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  String result = "";
  
  if (days > 0)
  {
    result += String(days) + "d ";
  }
  if (hours % 24 > 0)
  {
    result += String(hours % 24) + "h ";
  }
  if (minutes % 60 > 0)
  {
    result += String(minutes % 60) + "m ";
  }
  if (seconds % 60 > 0)
  {
    result += String(seconds % 60) + "s";
  }
  
  // If all values are 0, return "0s"
  if (result.length() == 0)
  {
    result = "0s";
  }
  else
  {
    // Trim trailing space if present
    result.trim();
  }
  
  return result;
}

// Format time difference between two timestamps (e.g., "2m 15s")
String formatRelativeTime(unsigned long fromMillis, unsigned long toMillis)
{
  unsigned long diffMillis = toMillis - fromMillis;
  unsigned long seconds = diffMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  String result = "";
  
  if (days > 0)
  {
    result += String(days) + "d ";
  }
  if (hours % 24 > 0)
  {
    result += String(hours % 24) + "h ";
  }
  if (minutes % 60 > 0)
  {
    result += String(minutes % 60) + "m ";
  }
  if (seconds % 60 > 0 || result.length() == 0)
  {
    result += String(seconds % 60) + "s";
  }
  
  result.trim();
  return result;
}

#endif

